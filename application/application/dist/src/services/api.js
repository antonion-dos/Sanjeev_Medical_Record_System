/**
 * Sanjeev Blockchain API Service
 * Communicates with the C++ node HTTP server, with a seamless offline
 * consensus fallback for testing when the node is offline.
 */

import { appState, DEMO_ACCOUNTS } from './state.js';
import { ClientCrypto } from './crypto.js';

export class BlockchainApi {
  constructor(baseUrl = 'http://localhost:8080') {
    this.baseUrl = baseUrl;
    this.isOnline = false;
    this.initLocalLedger();
  }

  async checkConnection() {
    try {
      const resp = await fetch(`${this.baseUrl}/api/status`, { signal: AbortSignal.timeout(1200) });
      if (resp.ok) {
        this.isOnline = true;
        return true;
      }
    } catch {
      this.isOnline = false;
    }
    return false;
  }

  // --- Initial Seed Ledger for Offline / Standalone Resilience ---
  async initLocalLedger() {
    if (this.localInitialized) return;
    this.localInitialized = true;

    // Create Initial Seed Record: Alice's Cardiac Profile
    const plainTextRecord = 
      "PATIENT: Alice Sharma | AGE: 34 | BLOOD GROUP: B+\n" +
      "DIAGNOSIS: Mild Cardiac Arrhythmia & Hypercholesterolemia\n" +
      "PRESCRIPTION: Atorvastatin 10mg OD, Metoprolol 25mg BD\n" +
      "NOTES: Patient exhibits episodic tachycardia after exertion. ECG shows normal sinus rhythm with occasional PACs.\n" +
      "FOLLOW-UP: 4 weeks with lipid profile and ambulatory Holter monitor.";

    const docKey = await ClientCrypto.generateKey();
    const docKeyHex = await ClientCrypto.exportKeyHex(docKey);
    const encData = await ClientCrypto.encrypt(plainTextRecord, docKey);
    const recordHash = await ClientCrypto.sha256(plainTextRecord);

    const now = Math.floor(Date.now() / 1000);
    const grantValidUntil = now + 86400; // 24 hours
    const delegateValidUntil = now + 43200; // 12 hours

    const initialRecordTx = {
      tx_id: '0x' + (await ClientCrypto.sha256('rec-1')).substring(0, 40),
      type: 'RECORD_STORE',
      sender: DEMO_ACCOUNTS.patient.address,
      recipient: '0x0000000000000000000000000000000000000000',
      timestamp: now - 3600,
      record_hash: recordHash,
      payload: JSON.stringify({
        title: 'Cardiology Evaluation & Lab Panel',
        patient_name: 'Alice Sharma',
        department: 'Cardiology',
        hospital_issuer: 'Apollo City Hospital',
        ciphertext: encData.ciphertext_b64,
        iv: encData.iv_b64,
        tag: encData.tag_b64,
        algorithm: 'AES-256-GCM',
        document_sym_key_hex: docKeyHex
      })
    };

    const initialGrantTx = {
      tx_id: '0x' + (await ClientCrypto.sha256('grant-1')).substring(0, 40),
      type: 'TEMPORAL_KEY_GRANT',
      sender: DEMO_ACCOUNTS.patient.address,
      recipient: DEMO_ACCOUNTS.hospital.address,
      timestamp: now - 1800,
      valid_from: now - 1800,
      valid_until: grantValidUntil,
      record_hash: recordHash,
      parent_tx_id: '',
      payload: JSON.stringify({
        grant_purpose: 'Outpatient Cardiology Consultation',
        authorized_document_sym_key_hex: docKeyHex
      })
    };

    const initialDelegateTx = {
      tx_id: '0x' + (await ClientCrypto.sha256('delegate-1')).substring(0, 40),
      type: 'TEMPORAL_KEY_DELEGATE',
      sender: DEMO_ACCOUNTS.hospital.address,
      recipient: DEMO_ACCOUNTS.doctor_rajesh.address,
      parent_tx_id: initialGrantTx.tx_id,
      timestamp: now - 900,
      valid_from: now - 900,
      valid_until: delegateValidUntil,
      record_hash: recordHash,
      payload: JSON.stringify({
        doctor_name: 'Dr. Rajesh Sharma, MD',
        specialty: 'Cardiology',
        authorized_document_sym_key_hex: docKeyHex
      })
    };

    this.localLedger = {
      blocks: [
        {
          index: 0,
          hash: '0x8f2a149b01c382f1bda829c0117498cda9281729381726354819283746152431',
          prev_hash: '0x0000000000000000000000000000000000000000000000000000000000000000',
          timestamp: now - 7200,
          authority_address: DEMO_ACCOUNTS.authority.address,
          authority_name: DEMO_ACCOUNTS.authority.name,
          transactions: [
            {
              tx_id: '0xgenesis_tx',
              type: 'AUTHORITY_REGISTER',
              sender: DEMO_ACCOUNTS.authority.address,
              recipient: DEMO_ACCOUNTS.authority.address,
              timestamp: now - 7200,
              payload: '{"genesis":"Sanjeev PoA Ledger Initialized"}'
            }
          ]
        },
        {
          index: 1,
          hash: '0x9a8b7c6d5e4f3a2b1c0d9e8f7a6b5c4d3e2f1a0b9c8d7e6f5a4b3c2d1e0f9a8b',
          prev_hash: '0x8f2a149b01c382f1bda829c0117498cda9281729381726354819283746152431',
          timestamp: now - 1800,
          authority_address: DEMO_ACCOUNTS.authority.address,
          authority_name: DEMO_ACCOUNTS.authority.name,
          transactions: [initialRecordTx, initialGrantTx, initialDelegateTx]
        }
      ],
      transactions: [initialRecordTx, initialGrantTx, initialDelegateTx],
      revokedKeys: {}
    };

    appState.records = [initialRecordTx];
    appState.temporalKeys = [initialGrantTx, initialDelegateTx];
    appState.blocks = this.localLedger.blocks;
    appState.notify();
  }

  // --- API Endpoints ---

  async getStatus() {
    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/status`);
        return await res.json();
      } catch (err) {
        console.warn('Fallback to local state for getStatus:', err);
      }
    }
    return {
      name: 'Sanjeev Blockchain Node (Offline/Embedded Mode)',
      chain_height: this.localLedger.blocks.length,
      current_timestamp: Math.floor(Date.now() / 1000),
      authority_node: DEMO_ACCOUNTS.authority.name,
      authority_address: DEMO_ACCOUNTS.authority.address,
      is_chain_valid: true
    };
  }

  async getBlocks() {
    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/blocks`);
        const blocks = await res.json();
        appState.blocks = blocks;
        return blocks;
      } catch (err) {
        console.warn('Fallback to local blocks:', err);
      }
    }
    return this.localLedger.blocks;
  }

  async getRecords(patientAddress = null) {
    if (await this.checkConnection()) {
      try {
        const url = patientAddress 
          ? `${this.baseUrl}/api/records?patient=${patientAddress}`
          : `${this.baseUrl}/api/records`;
        const res = await fetch(url);
        const data = await res.json();
        appState.records = data;
        return data;
      } catch (err) {
        console.warn('Fallback to local records:', err);
      }
    }
    const list = this.localLedger.transactions.filter(t => t.type === 'RECORD_STORE');
    if (patientAddress) {
      return list.filter(t => t.sender.toLowerCase() === patientAddress.toLowerCase());
    }
    return list;
  }

  async submitRecord(recordPayload, patientAddress) {
    const now = Math.floor(Date.now() / 1000);
    const tx = {
      tx_id: '0x' + (await ClientCrypto.sha256(JSON.stringify(recordPayload) + now)).substring(0, 40),
      type: 'RECORD_STORE',
      sender: patientAddress,
      recipient: '0x0000000000000000000000000000000000000000',
      timestamp: now,
      record_hash: await ClientCrypto.sha256(recordPayload.ciphertext),
      payload: JSON.stringify(recordPayload)
    };

    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/records`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(tx)
        });
        if (res.ok) {
          await this.getRecords();
          return await res.json();
        }
      } catch (err) {
        console.warn('Failed to submit to remote node, recording locally:', err);
      }
    }

    // Local ledger fallback
    this.localLedger.transactions.push(tx);
    // Seal block
    const newBlock = {
      index: this.localLedger.blocks.length,
      hash: '0x' + (await ClientCrypto.sha256(tx.tx_id + now)).substring(0, 64),
      prev_hash: this.localLedger.blocks[this.localLedger.blocks.length - 1].hash,
      timestamp: now,
      authority_address: DEMO_ACCOUNTS.authority.address,
      authority_name: DEMO_ACCOUNTS.authority.name,
      transactions: [tx]
    };
    this.localLedger.blocks.push(newBlock);

    appState.records = this.localLedger.transactions.filter(t => t.type === 'RECORD_STORE');
    appState.blocks = this.localLedger.blocks;
    appState.notify();
    return { success: true, tx_id: tx.tx_id };
  }

  async grantTemporalKey(grantData) {
    const now = Math.floor(Date.now() / 1000);
    const tx = {
      tx_id: '0x' + (await ClientCrypto.sha256('grant-' + now + grantData.recipient)).substring(0, 40),
      type: 'TEMPORAL_KEY_GRANT',
      sender: grantData.sender,
      recipient: grantData.recipient,
      timestamp: now,
      valid_from: grantData.valid_from || now,
      valid_until: grantData.valid_until,
      record_hash: grantData.record_hash,
      parent_tx_id: '',
      payload: JSON.stringify(grantData.payload)
    };

    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/keys/temporal`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(tx)
        });
        if (res.ok) return await res.json();
      } catch (err) {
        console.warn('Remote grant failed, executing locally:', err);
      }
    }

    this.localLedger.transactions.push(tx);
    appState.temporalKeys = this.localLedger.transactions.filter(
      t => t.type === 'TEMPORAL_KEY_GRANT' || t.type === 'TEMPORAL_KEY_DELEGATE'
    );
    appState.notify();
    return { success: true, tx_id: tx.tx_id };
  }

  async delegateTemporalKey(delegateData) {
    const now = Math.floor(Date.now() / 1000);
    const tx = {
      tx_id: '0x' + (await ClientCrypto.sha256('del-' + now + delegateData.recipient)).substring(0, 40),
      type: 'TEMPORAL_KEY_DELEGATE',
      sender: delegateData.sender,
      recipient: delegateData.recipient,
      parent_tx_id: delegateData.parent_tx_id,
      timestamp: now,
      valid_from: delegateData.valid_from || now,
      valid_until: delegateData.valid_until,
      record_hash: delegateData.record_hash,
      payload: JSON.stringify(delegateData.payload)
    };

    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/keys/delegate`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(tx)
        });
        if (res.ok) return await res.json();
      } catch (err) {
        console.warn('Remote delegate failed, executing locally:', err);
      }
    }

    this.localLedger.transactions.push(tx);
    appState.temporalKeys = this.localLedger.transactions.filter(
      t => t.type === 'TEMPORAL_KEY_GRANT' || t.type === 'TEMPORAL_KEY_DELEGATE'
    );
    appState.notify();
    return { success: true, tx_id: tx.tx_id };
  }

  async revokeTemporalKey(keyId) {
    this.localLedger.revokedKeys[keyId] = true;
    appState.temporalKeys = appState.temporalKeys.filter(k => k.tx_id !== keyId);
    appState.notify();
    return { success: true };
  }

  async getBirdsEyeView() {
    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.baseUrl}/api/birds_eye`);
        if (res.ok) return await res.json();
      } catch (err) {
        console.warn('Fallback to local birds-eye view:', err);
      }
    }

    const now = Math.floor(Date.now() / 1000);
    const distinctAddresses = new Set();
    const links = [];
    const keysArr = [];

    for (const tx of this.localLedger.transactions) {
      if (tx.type === 'TEMPORAL_KEY_GRANT' || tx.type === 'TEMPORAL_KEY_DELEGATE') {
        distinctAddresses.add(tx.sender);
        distinctAddresses.add(tx.recipient);

        const isRevoked = !!this.localLedger.revokedKeys[tx.tx_id];
        const isExpired = tx.valid_until > 0 && now > tx.valid_until;
        const isActive = !isRevoked && !isExpired;

        keysArr.push({
          tx_id: tx.tx_id,
          type: tx.type,
          sender: tx.sender,
          recipient: tx.recipient,
          record_hash: tx.record_hash,
          parent_tx_id: tx.parent_tx_id,
          valid_from: tx.valid_from,
          valid_until: tx.valid_until,
          is_active: isActive,
          is_expired: isExpired,
          is_revoked: isRevoked,
          time_remaining_seconds: Math.max(0, tx.valid_until - now)
        });

        links.push({
          from: tx.sender,
          to: tx.recipient,
          key_id: tx.tx_id,
          record_hash: tx.record_hash,
          parent_tx_id: tx.parent_tx_id,
          is_active: isActive,
          type: tx.type
        });
      } else if (tx.type === 'RECORD_STORE') {
        distinctAddresses.add(tx.sender);
      }
    }

    return {
      chain_height: this.localLedger.blocks.length,
      total_transactions: this.localLedger.transactions.length,
      mempool_size: 0,
      current_time: now,
      authorities: [
        { address: DEMO_ACCOUNTS.authority.address, name: DEMO_ACCOUNTS.authority.name }
      ],
      blocks: this.localLedger.blocks.map(b => ({
        index: b.index,
        hash: b.hash,
        prev_hash: b.prev_hash,
        timestamp: b.timestamp,
        authority_name: b.authority_name,
        tx_count: b.transactions.length
      })),
      temporal_keys: keysArr,
      lineage_graph: {
        nodes: Array.from(distinctAddresses).map(addr => ({ address: addr })),
        links
      }
    };
  }
}

export const api = new BlockchainApi();
