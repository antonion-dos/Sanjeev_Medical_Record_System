/**
 * Sanjeev Blockchain API Service
 * Communicates with the C++ node HTTP server, with a seamless offline
 * consensus fallback for testing when the node is offline.
 */

import { appState, DEMO_ACCOUNTS } from './state.js';
import { ClientCrypto } from './crypto.js';
import { ConfigManager } from '../config/node-config.js';

export class BlockchainApi {
  constructor(baseUrl = null) {
    this.configuredUrl = baseUrl;
    this.isOnline = false;
    this.lastLatencyMs = null;
    this.initLocalLedger();
  }

  getBaseUrl() {
    return this.configuredUrl || ConfigManager.getNodeUrl();
  }

  setBaseUrl(url) {
    this.configuredUrl = ConfigManager.setNodeUrl(url);
  }

  async verifyNodeConnection(customUrl = null) {
    const targetUrl = customUrl ? customUrl.trim().replace(/\/+$/, '') : this.getBaseUrl();
    const start = performance.now();
    try {
      const resp = await fetch(`${targetUrl}/api/v1/chain/status`, {
        signal: AbortSignal.timeout(3000)
      });
      const latencyMs = Math.round(performance.now() - start);
      if (resp.ok) {
        const data = await resp.json();
        this.isOnline = true;
        this.lastLatencyMs = latencyMs;
        return {
          success: true,
          latencyMs,
          chainHeight: data.chain_height ?? 0,
          mempoolSize: data.mempool_size ?? 0,
          authorityName: data.authority_name || 'Ministry of Health',
          url: targetUrl
        };
      }
      return {
        success: false,
        latencyMs,
        error: `HTTP ${resp.status}: ${resp.statusText}`,
        url: targetUrl
      };
    } catch (err) {
      const latencyMs = Math.round(performance.now() - start);
      return {
        success: false,
        latencyMs,
        error: err.name === 'TimeoutError' ? 'Connection Timed Out' : (err.message || 'Connection Refused'),
        url: targetUrl
      };
    }
  }

  async checkConnection() {
    try {
      const resp = await fetch(`${this.getBaseUrl()}/api/v1/chain/status`, {
        signal: AbortSignal.timeout(1200)
      });
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

    try {
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

      const doctorAddress = (DEMO_ACCOUNTS.doctor_rajesh || (DEMO_ACCOUNTS.hospital.doctors && DEMO_ACCOUNTS.hospital.doctors[0]) || { address: '0x38AF44cE15993a4dB2e9575fDb2b28F10815B452' }).address;

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
        recipient: doctorAddress,
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
    } catch (err) {
      console.error('Error initializing local ledger:', err);
      this.localLedger = { blocks: [], transactions: [], revokedKeys: {} };
    }
  }

  // --- API Endpoints ---

  async getStatus() {
    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.getBaseUrl()}/api/v1/chain/status`);
        const data = await res.json();
        return {
          name: 'Sanjeev PoA Blockchain Node',
          chain_height: data.chain_height,
          mempool_size: data.mempool_size,
          authority_node: data.authority_name || DEMO_ACCOUNTS.authority.name,
          authority_address: DEMO_ACCOUNTS.authority.address,
          is_chain_valid: true
        };
      } catch (err) {
        console.warn('Fallback to local state for getStatus:', err);
      }
    }
    return {
      name: 'Sanjeev Blockchain Node (Offline/Embedded Mode)',
      chain_height: (this.localLedger && this.localLedger.blocks) ? this.localLedger.blocks.length : 0,
      current_timestamp: Math.floor(Date.now() / 1000),
      authority_node: DEMO_ACCOUNTS.authority.name,
      authority_address: DEMO_ACCOUNTS.authority.address,
      is_chain_valid: true
    };
  }

  async getBlocks() {
    if (await this.checkConnection()) {
      try {
        const res = await fetch(`${this.getBaseUrl()}/api/v1/chain/blocks`);
        if (res.ok) {
          const blocks = await res.json();
          appState.blocks = blocks;
          return blocks;
        }
      } catch (err) {
        console.warn('Fallback to local blocks:', err);
      }
    }
    return (this.localLedger && this.localLedger.blocks) ? this.localLedger.blocks : [];
  }

  async getRecords(patientAddress = null) {
    const list = (this.localLedger && this.localLedger.transactions) 
      ? this.localLedger.transactions.filter(t => t.type === 'RECORD_STORE')
      : [];

    if (patientAddress) {
      return list.filter(t => t.sender.toLowerCase() === patientAddress.toLowerCase());
    }
    return list;
  }

  async submitRecord(recordPayload, patientAddress) {
    const now = Math.floor(Date.now() / 1000);
    const rawHash = await ClientCrypto.sha256(recordPayload.ciphertext || JSON.stringify(recordPayload));
    const blobId = rawHash.startsWith('0x') ? rawHash : '0x' + rawHash;
    const ivHex = recordPayload.iv_hex || ClientCrypto.base64ToHex(recordPayload.iv);
    const tagHex = recordPayload.tag_hex || ClientCrypto.base64ToHex(recordPayload.tag);

    const tx = {
      tx_id: blobId.substring(0, 42),
      type: 'RECORD_STORE',
      sender: patientAddress,
      recipient: '0x0000000000000000000000000000000000000000',
      timestamp: now,
      record_hash: blobId,
      payload: JSON.stringify(recordPayload)
    };

    // 1. Submit to Live C++ Blockchain Node if Online
    if (await this.checkConnection()) {
      try {
        const nodeBlobTx = {
          sender: patientAddress,
          nonce: Date.now(),
          payload: {
            blob_id: blobId,
            previous_blob_id: '0x0000000000000000000000000000000000000000000000000000000000000000',
            owner_address: patientAddress,
            iv_hex: ivHex,
            tag_hex: tagHex,
            data_b64: recordPayload.ciphertext
          }
        };

        const res = await fetch(`${this.getBaseUrl()}/api/v1/blob/store`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(nodeBlobTx)
        });

        if (res.ok) {
          // Immediately seal block so it appears in the ledger
          await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: '{}'
          });
          await this.getBlocks();
        }
      } catch (err) {
        console.warn('Failed to submit to remote node, recording locally:', err);
      }
    }

    // 2. Local ledger cache update
    if (this.localLedger && this.localLedger.transactions) {
      this.localLedger.transactions.push(tx);
      const newBlock = {
        index: this.localLedger.blocks.length,
        hash: '0x' + (await ClientCrypto.sha256(tx.tx_id + now)).substring(0, 64),
        prev_hash: this.localLedger.blocks[this.localLedger.blocks.length - 1]?.hash || '0x00',
        timestamp: now,
        authority_address: DEMO_ACCOUNTS.authority.address,
        authority_name: DEMO_ACCOUNTS.authority.name,
        transactions: [tx]
      };
      this.localLedger.blocks.push(newBlock);
      appState.records = this.localLedger.transactions.filter(t => t.type === 'RECORD_STORE');
      appState.blocks = this.localLedger.blocks;
    }
    appState.notify();
    return { success: true, tx_id: tx.tx_id, blob_id: blobId };
  }

  async grantTemporalKey(grantData) {
    const now = Math.floor(Date.now() / 1000);
    const rawTokenHash = await ClientCrypto.sha256('grant-' + now + grantData.recipient + grantData.record_hash);
    const tokenId = rawTokenHash.startsWith('0x') ? rawTokenHash : '0x' + rawTokenHash;

    const tx = {
      tx_id: tokenId.substring(0, 42),
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

    // 1. Submit to Live C++ Blockchain Node if Online
    if (await this.checkConnection()) {
      try {
        const rawTargetBlob = grantData.record_hash || '';
        const cleanTargetBlob = rawTargetBlob.startsWith('0x') ? rawTargetBlob : '0x' + rawTargetBlob;
        const paddedTargetBlob = cleanTargetBlob.padEnd(66, '0').substring(0, 66);

        const nodeTokenTx = {
          sender: grantData.sender,
          nonce: Date.now(),
          payload: {
            token_id: tokenId.padEnd(66, '0').substring(0, 66),
            target_blob_id: paddedTargetBlob,
            grantor_address: grantData.sender,
            recipient_address: grantData.recipient,
            valid_from: grantData.valid_from || now,
            valid_until: grantData.valid_until,
            encrypted_symkey_hex: grantData.payload?.authorized_document_sym_key_hex || 'cafebabe1234'
          }
        };

        const res = await fetch(`${this.getBaseUrl()}/api/v1/token/grant`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify(nodeTokenTx)
        });

        if (res.ok) {
          await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: '{}'
          });
          await this.getBlocks();
        }
      } catch (err) {
        console.warn('Remote grant failed, executing locally:', err);
      }
    }

    // 2. Local ledger cache update
    if (this.localLedger && this.localLedger.transactions) {
      this.localLedger.transactions.push(tx);
      appState.temporalKeys = this.localLedger.transactions.filter(
        t => t.type === 'TEMPORAL_KEY_GRANT' || t.type === 'TEMPORAL_KEY_DELEGATE'
      );
    }
    appState.notify();
    return { success: true, tx_id: tx.tx_id, token_id: tokenId };
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

    if (this.localLedger && this.localLedger.transactions) {
      this.localLedger.transactions.push(tx);
      appState.temporalKeys = this.localLedger.transactions.filter(
        t => t.type === 'TEMPORAL_KEY_GRANT' || t.type === 'TEMPORAL_KEY_DELEGATE'
      );
    }
    appState.notify();
    return { success: true, tx_id: tx.tx_id };
  }

  async revokeTemporalKey(keyId, senderAddress = null) {
    if (await this.checkConnection()) {
      try {
        const sender = senderAddress || (appState.currentUser ? appState.currentUser.address : DEMO_ACCOUNTS.patient.address);
        const cleanTokenId = keyId.startsWith('0x') ? keyId : '0x' + keyId;
        const paddedTokenId = cleanTokenId.padEnd(66, '0').substring(0, 66);

        await fetch(`${this.getBaseUrl()}/api/v1/token/revoke`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            sender: sender,
            nonce: Date.now(),
            payload: {
              target_token_id: paddedTokenId,
              reason: 'Patient revoked access early'
            }
          })
        });
        await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}'
        });
      } catch (err) {
        console.warn('On-chain token revocation failed:', err);
      }
    }

    if (this.localLedger && this.localLedger.revokedKeys) {
      this.localLedger.revokedKeys[keyId] = true;
    }
    appState.temporalKeys = appState.temporalKeys.filter(k => k.tx_id !== keyId);
    appState.notify();
    return { success: true };
  }

  async requestDecryption(tokenId, accessorAddress = null) {
    if (await this.checkConnection()) {
      try {
        const accessor = accessorAddress || (appState.currentUser ? appState.currentUser.address : DEMO_ACCOUNTS.doctor_rajesh.address);
        const cleanTokenId = tokenId.startsWith('0x') ? tokenId : '0x' + tokenId;
        const paddedTokenId = cleanTokenId.padEnd(66, '0').substring(0, 66);

        const res = await fetch(`${this.getBaseUrl()}/api/v1/token/audit_decrypt`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            token_id: paddedTokenId,
            accessor_address: accessor,
            timestamp: Math.floor(Date.now() / 1000)
          })
        });

        if (res.status === 403) {
          throw new Error('Access Denied: Temporal token is expired or revoked by patient.');
        }

        if (res.ok) {
          const data = await res.json();
          // Mine block to seal decryption audit receipt on-chain
          await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: '{}'
          });
          return data.encrypted_symkey_hex;
        }
      } catch (err) {
        console.warn('On-chain audit decryption check encountered error:', err.message);
        throw err;
      }
    }
    return null;
  }

  async getBirdsEyeView() {
    let chainHeight = (this.localLedger && this.localLedger.blocks) ? this.localLedger.blocks.length : 0;
    let nodeBlocks = [];
    let syncData = null;

    // 1. Fetch live ledger sync if server endpoint is available
    try {
      const syncRes = await fetch('/api/ledger/sync');
      if (syncRes.ok) {
        syncData = await syncRes.json();
      }
    } catch (e) {
      console.warn('Ledger sync endpoint unavailable, using node REST fallback:', e);
    }

    if (syncData && Array.isArray(syncData.blocks) && syncData.blocks.length > 0) {
      nodeBlocks = syncData.blocks.map(b => ({
        index: b.block_index,
        hash: b.block_hash,
        prev_hash: b.prev_hash,
        merkle_root: b.merkle_root,
        timestamp: b.timestamp,
        authority_name: b.authority_name,
        tx_count: b.tx_count
      }));
      chainHeight = nodeBlocks.length;
    } else if (await this.checkConnection()) {
      try {
        nodeBlocks = await this.getBlocks();
        if (Array.isArray(nodeBlocks) && nodeBlocks.length > 0) {
          chainHeight = nodeBlocks.length;
        }
      } catch (err) {
        console.warn('Fallback to local blocks for birds-eye view:', err);
      }
    }

    const now = Math.floor(Date.now() / 1000);
    const distinctAddresses = new Set();
    const links = [];
    const keysArr = [];

    // Local in-memory transactions
    const localTxs = (this.localLedger && this.localLedger.transactions) ? this.localLedger.transactions : [];

    for (const tx of localTxs) {
      if (tx.type === 'TEMPORAL_KEY_GRANT' || tx.type === 'TEMPORAL_KEY_DELEGATE') {
        if (tx.sender) distinctAddresses.add(tx.sender);
        if (tx.recipient) distinctAddresses.add(tx.recipient);

        const isRevoked = this.localLedger?.revokedKeys?.[tx.tx_id] ?? false;
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
        if (tx.sender) distinctAddresses.add(tx.sender);
      }
    }

    // 2. Incorporate live on-chain tokens from SQLite ledger
    if (syncData && Array.isArray(syncData.tokens)) {
      for (const t of syncData.tokens) {
        const normTokenId = t.token_id;
        if (keysArr.some(k => k.tx_id === normTokenId)) continue;

        if (t.grantor_address) distinctAddresses.add(t.grantor_address);
        if (t.recipient_address) distinctAddresses.add(t.recipient_address);

        const isRevoked = t.status === 2 || (this.localLedger?.revokedKeys?.[normTokenId] ?? false);
        const isExpired = t.valid_until > 0 && now > t.valid_until;
        const isActive = t.status === 1 && !isRevoked && !isExpired;

        keysArr.push({
          tx_id: normTokenId,
          type: (t.parent_token_id && t.parent_token_id !== '0x0000000000000000000000000000000000000000000000000000000000000000')
            ? 'TEMPORAL_KEY_DELEGATE'
            : 'TEMPORAL_KEY_GRANT',
          sender: t.grantor_address,
          recipient: t.recipient_address,
          record_hash: t.target_blob_id,
          parent_tx_id: t.parent_token_id || '',
          valid_from: t.valid_from,
          valid_until: t.valid_until,
          is_active: isActive,
          is_expired: isExpired,
          is_revoked: isRevoked,
          time_remaining_seconds: Math.max(0, t.valid_until - now)
        });

        links.push({
          from: t.grantor_address,
          to: t.recipient_address,
          key_id: normTokenId,
          record_hash: t.target_blob_id,
          parent_tx_id: t.parent_token_id,
          is_active: isActive,
          type: 'TEMPORAL_KEY_GRANT'
        });
      }
    }

    // 3. Assemble visual Patient -> Hospital -> Doctor entity chains
    const chains = [
      {
        patient: {
          name: DEMO_ACCOUNTS.patient.name,
          role: 'Patient',
          address: DEMO_ACCOUNTS.patient.address,
          label: 'Patient Alice (Owner)'
        },
        hospital: {
          name: DEMO_ACCOUNTS.hospital.name,
          role: 'Hospital',
          address: DEMO_ACCOUNTS.hospital.address,
          label: 'Apollo Hospital (Facility)'
        },
        doctor: {
          name: DEMO_ACCOUNTS.doctor_rajesh.name,
          role: 'Doctor',
          address: DEMO_ACCOUNTS.doctor_rajesh.address,
          label: 'Dr. Rajesh Sharma (Specialist)'
        },
        grantActive: true,
        delActive: true,
        grantLabel: '⏱️ Master Grant (24h Limit)',
        delLabel: '➡️ Sub-Delegated (12h Limit)'
      }
    ];

    if (syncData && Array.isArray(syncData.entities)) {
      for (const ent of syncData.entities) {
        if (ent.patient?.address) distinctAddresses.add(ent.patient.address);
        if (ent.hospital?.address) distinctAddresses.add(ent.hospital.address);
        if (ent.doctor?.address) distinctAddresses.add(ent.doctor.address);

        chains.push({
          patient: {
            name: ent.patient?.name || 'Patient',
            role: 'Patient',
            address: ent.patient?.address || '',
            label: `${ent.patient?.name || 'Patient'} (Owner)`
          },
          hospital: {
            name: ent.hospital?.name || 'Hospital',
            role: 'Hospital',
            address: ent.hospital?.address || '',
            label: `${ent.hospital?.name || 'Hospital'} (Facility)`
          },
          doctor: {
            name: ent.doctor?.name || 'Doctor',
            role: 'Doctor',
            address: ent.doctor?.address || '',
            label: `${ent.doctor?.name || 'Doctor'} (${ent.doctor?.specialty || 'Specialist'})`
          },
          grantActive: true,
          delActive: true,
          grantLabel: `⏱️ Access Grant (${ent.token?.duration_hours || 24}h Limit)`,
          delLabel: '🔓 Decryption Audited On-Chain'
        });
      }
    }

    const totalTxCount = (nodeBlocks && nodeBlocks.length > 0)
      ? nodeBlocks.reduce((acc, b) => acc + (b.tx_count || 0), 0)
      : localTxs.length;

    const displayBlocks = (nodeBlocks && nodeBlocks.length > 0)
      ? nodeBlocks
      : (this.localLedger?.blocks || []).map(b => ({
          index: b.index,
          hash: b.hash,
          prev_hash: b.prev_hash,
          timestamp: b.timestamp,
          authority_name: b.authority_name,
          tx_count: b.transactions?.length || 0
        }));

    return {
      chain_height: chainHeight,
      total_transactions: totalTxCount,
      mempool_size: 0,
      current_time: now,
      authorities: [
        { address: DEMO_ACCOUNTS.authority.address, name: DEMO_ACCOUNTS.authority.name }
      ],
      blocks: displayBlocks,
      temporal_keys: keysArr,
      lineage_graph: {
        nodes: Array.from(distinctAddresses).map(addr => ({ address: addr })),
        links,
        chains
      }
    };
  }
}

export const api = new BlockchainApi();
