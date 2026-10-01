/**
 * Sanjeev - Hospital & Doctor View Component
 * Scales for both large hospital networks and independent physicians.
 * Tracks patient temporal keys, enables key delegation to doctors in the hospital database,
 * and handles time-gated clinical record decryption.
 */

import { appState, DEMO_ACCOUNTS } from '../services/state.js';
import { api } from '../services/api.js';
import { ClientCrypto } from '../services/crypto.js';

export class HospitalView {
  constructor(container) {
    this.container = container;
    this.activeSubView = 'keys'; // 'keys' | 'doctors' | 'decrypt'
    this.decryptedData = {}; // keyed by tx_id
    this.selectedKeyForDelegate = null;
  }

  async render() {
    const user = appState.currentUser;
    const isHospitalOrg = user.role === 'hospital';
    const now = Math.floor(Date.now() / 1000);

    const allKeys = appState.temporalKeys;
    // Keys relevant to this account (either recipient or sender)
    const myKeys = allKeys.filter(k => 
      k.recipient.toLowerCase() === user.address.toLowerCase() ||
      k.sender.toLowerCase() === user.address.toLowerCase()
    );

    this.container.innerHTML = `
      <div class="view-container">
        <!-- View Header -->
        <div class="view-header">
          <div class="view-title-group">
            <div style="display: flex; align-items: center; gap: 0.75rem;">
              <h2>${isHospitalOrg ? 'Hospital Clinical Operations' : 'Physician Clinical Portal'}</h2>
              <span class="tag ${isHospitalOrg ? 'tag-info' : 'tag-success'}">
                ${isHospitalOrg ? 'ENTERPRISE FACILITY' : 'INDEPENDENT DOCTOR'}
              </span>
            </div>
            <div class="view-subtitle">
              ${isHospitalOrg 
                ? 'Centralized Patient Registry & Hierarchical Doctor Access Delegation' 
                : `${user.specialty || 'General Practice'} &bull; Affiliated: ${user.hospital || 'Private Clinic'}`}
              &bull; Account: <span class="address-pill">${user.address}</span>
            </div>
          </div>

          <div style="display: flex; gap: 0.5rem; align-items: center;">
            <label style="font-size: 0.85rem; color: var(--text-muted);">Switch View:</label>
            <select class="form-select" id="hospital-role-switch" style="padding: 0.35rem 0.6rem; font-size: 0.82rem;">
              <option value="hospital" ${user.role === 'hospital' ? 'selected' : ''}>
                Apollo City Hospital (Enterprise)
              </option>
              <option value="doctor_rajesh" ${user === DEMO_ACCOUNTS.doctor_rajesh ? 'selected' : ''}>
                Dr. Rajesh Sharma (Cardiologist)
              </option>
              <option value="doctor_priya" ${user === DEMO_ACCOUNTS.doctor_priya ? 'selected' : ''}>
                Dr. Priya Patel (Endocrinologist)
              </option>
            </select>
          </div>
        </div>

        <!-- Sub Tabs -->
        <div style="display: flex; gap: 0.5rem; border-bottom: 1px solid var(--border-color); padding-bottom: 0.5rem;">
          <button class="btn ${this.activeSubView === 'keys' ? 'btn-primary' : 'btn-secondary'} btn-sm" id="sub-keys">
            Patient Temporal Keys (${myKeys.length})
          </button>
          ${isHospitalOrg ? `
            <button class="btn ${this.activeSubView === 'doctors' ? 'btn-primary' : 'btn-secondary'} btn-sm" id="sub-doctors">
              Doctor Staff Database (${appState.hospitalDoctors.length})
            </button>
          ` : ''}
        </div>

        <!-- Sub Contents -->
        <div id="hospital-content">
          ${this.activeSubView === 'doctors' && isHospitalOrg
            ? this.renderDoctorsTab()
            : this.renderKeysTab(myKeys, now, isHospitalOrg)}
        </div>
      </div>

      <div id="hospital-modal-root"></div>
    `;

    this.attachEventListeners(myKeys);
  }

  renderKeysTab(myKeys, now, isHospitalOrg) {
    if (myKeys.length === 0) {
      return `
        <div class="card" style="text-align: center; padding: 3rem;">
          <h3 style="margin-bottom: 0.5rem;">No Patient Access Keys Found</h3>
          <p class="text-muted">No patient has granted or delegated an active temporal access key to this address.</p>
        </div>
      `;
    }

    return `
      <div style="display: flex; flex-direction: column; gap: 1rem;">
        ${myKeys.map(k => {
          let payload = {};
          try { payload = JSON.parse(k.payload); } catch {}
          const isExpired = k.valid_until > 0 && now > k.valid_until;
          const timeLeft = Math.max(0, k.valid_until - now);
          const hours = Math.floor(timeLeft / 3600);
          const minutes = Math.floor((timeLeft % 3600) / 60);
          const isDelegation = k.type === 'TEMPORAL_KEY_DELEGATE';
          const isDecrypted = !!this.decryptedData[k.tx_id];

          return `
            <div class="card" style="padding: 1.25rem;">
              <div class="key-row">
                <div style="display: flex; align-items: center; gap: 0.75rem;">
                  <span class="tag ${isExpired ? 'tag-danger' : 'tag-success'}">
                    ${isExpired ? 'EXPIRED' : 'ACTIVE ACCESS'}
                  </span>
                  <span class="tag ${isDelegation ? 'tag-warning' : 'tag-info'}">
                    ${isDelegation ? 'DELEGATED' : 'PATIENT GRANT'}
                  </span>
                  <strong style="font-size: 1.05rem;">
                    ${payload.grant_purpose || payload.specialty || 'Patient Health Record Access'}
                  </strong>
                </div>

                <div class="countdown-box ${isExpired ? 'expired' : ''}">
                  ⏱️ ${isExpired ? 'Access Expired (Revoked by Blockchain)' : `${hours}h ${minutes}m Remaining`}
                </div>
              </div>

              <div class="grid-2" style="margin-top: 1rem; font-size: 0.85rem; color: var(--text-muted);">
                <div>
                  <strong>From:</strong> <span class="address-pill">${k.sender}</span>
                </div>
                <div>
                  <strong>To:</strong> <span class="address-pill">${k.recipient}</span>
                </div>
                <div>
                  <strong>Record Hash:</strong> <span class="address-pill">${k.record_hash ? k.record_hash.substring(0, 20) + '...' : 'N/A'}</span>
                </div>
                <div>
                  <strong>Valid Window:</strong> ${new Date(k.valid_from * 1000).toLocaleTimeString()} - ${new Date(k.valid_until * 1000).toLocaleString()}
                </div>
              </div>

              ${isDecrypted ? `
                <div class="record-decrypted" style="margin-top: 1rem;">
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 0.5rem; border-bottom: 1px solid rgba(16, 185, 129, 0.3); padding-bottom: 0.25rem;">
                    <strong style="color: var(--success);">🔓 Verified Decrypted Clinical Record</strong>
                    <span style="font-size: 0.75rem; color: var(--text-muted);">Decrypted locally via WebCrypto</span>
                  </div>
                  ${this.decryptedData[k.tx_id]}
                </div>
              ` : ''}

              <div style="display: flex; gap: 0.75rem; margin-top: 1rem; justify-content: flex-end; flex-wrap: wrap;">
                ${isHospitalOrg && !isExpired ? `
                  <button class="btn btn-primary btn-sm btn-delegate" data-keyid="${k.tx_id}" data-until="${k.valid_until}" data-hash="${k.record_hash}">
                    ➡️ Delegate to Doctor
                  </button>
                ` : ''}

                <button class="btn ${isExpired ? 'btn-secondary' : 'btn-success'} btn-sm btn-decrypt-clinical" 
                        data-keyid="${k.tx_id}" 
                        data-expired="${isExpired}">
                  ${isExpired ? '🔒 Expired (Cannot Decrypt)' : (isDecrypted ? 'Hide Record' : '🔓 Decrypt & Inspect Record')}
                </button>
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;
  }

  renderDoctorsTab() {
    const doctors = appState.hospitalDoctors;
    return `
      <div class="grid-2">
        <div class="card">
          <div class="card-header">
            <h3 class="card-title">👨‍⚕️ Affiliated Physicians Database</h3>
            <button class="btn btn-primary btn-sm" id="btn-add-doc">+ Add Doctor</button>
          </div>

          <div style="display: flex; flex-direction: column; gap: 0.75rem;">
            ${doctors.map(d => `
              <div style="background: var(--bg-primary); border: 1px solid var(--border-color); border-radius: var(--radius-md); padding: 1rem;">
                <div style="display: flex; justify-content: space-between; align-items: center;">
                  <strong>${d.name}</strong>
                  <span class="tag tag-info">${d.specialty}</span>
                </div>
                <div style="margin-top: 0.4rem; font-size: 0.8rem; color: var(--text-muted);">
                  Address: <span class="address-pill">${d.address}</span>
                </div>
                <div style="margin-top: 0.4rem; font-size: 0.8rem; color: var(--success);">
                  Active Delegated Cases: ${d.activeCases}
                </div>
              </div>
            `).join('')}
          </div>
        </div>

        <div class="card">
          <div class="card-header">
            <h3 class="card-title">📋 Hierarchical Delegation Architecture</h3>
            <span class="tag tag-success">Strict Multi-Sig Ready</span>
          </div>
          <p style="font-size: 0.9rem; color: var(--text-muted); line-height: 1.6;">
            In Sanjeev, a hospital facility acts as the sovereign custodian for patient grants.
            When patient <strong>Alice</strong> grants a 24-hour temporal key to the hospital, the hospital intake system can securely sub-delegate access to attending specialists (e.g. <strong>Dr. Rajesh Sharma</strong>).
          </p>
          <div style="background: rgba(14, 165, 233, 0.08); border: 1px solid var(--border-color); border-radius: var(--radius-sm); padding: 1rem; margin-top: 1rem; font-size: 0.85rem;">
            <strong>Cryptographic Delegation Guarantee:</strong>
            <ul style="margin-top: 0.5rem; margin-left: 1.25rem;">
              <li>Sub-delegated keys cannot exceed the parent expiration timestamp.</li>
              <li>Revoking the master patient grant cascades immediate revocation down the entire doctor hierarchy.</li>
              <li>The lineage is transparently auditable on the blockchain viewer.</li>
            </ul>
          </div>
        </div>
      </div>
    `;
  }

  attachEventListeners(myKeys) {
    this.container.querySelector('#sub-keys')?.addEventListener('click', () => {
      this.activeSubView = 'keys';
      this.render();
    });

    this.container.querySelector('#sub-doctors')?.addEventListener('click', () => {
      this.activeSubView = 'doctors';
      this.render();
    });

    // Switch between Hospital and Doctor view
    this.container.querySelector('#hospital-role-switch')?.addEventListener('change', (e) => {
      appState.setUser(e.target.value);
    });

    // Delegate Key Button
    this.container.querySelectorAll('.btn-delegate').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const keyId = e.currentTarget.dataset.keyid;
        const validUntil = parseInt(e.currentTarget.dataset.until, 10);
        const recordHash = e.currentTarget.dataset.hash;
        this.showDelegateModal(keyId, validUntil, recordHash);
      });
    });

    // Decrypt Clinical Record
    this.container.querySelectorAll('.btn-decrypt-clinical').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        const keyId = e.currentTarget.dataset.keyid;
        const isExpired = e.currentTarget.dataset.expired === 'true';

        if (isExpired) {
          alert('🚫 Access Denied: This temporal key has expired. Decryption cannot be authorized by the blockchain node.');
          return;
        }

        if (this.decryptedData[keyId]) {
          delete this.decryptedData[keyId];
          this.render();
          return;
        }

        const keyObj = myKeys.find(k => k.tx_id === keyId);
        if (!keyObj) return;

        let keyPayload = {};
        try { keyPayload = JSON.parse(keyObj.payload); } catch {}

        // Find the record
        const records = await api.getRecords();
        const targetRec = records.find(r => r.record_hash === keyObj.record_hash);

        if (!targetRec) {
          alert('Record not found on blockchain');
          return;
        }

        let docPayload = {};
        try { docPayload = JSON.parse(targetRec.payload); } catch {}

        try {
          const symKeyHex = keyPayload.authorized_document_sym_key_hex || docPayload.document_sym_key_hex;
          const key = await ClientCrypto.importKeyHex(symKeyHex);
          const decrypted = await ClientCrypto.decrypt(docPayload, key);
          this.decryptedData[keyId] = decrypted;
          this.render();
        } catch (err) {
          alert('Decryption failed: ' + err.message);
        }
      });
    });

    // Add Doctor button
    this.container.querySelector('#btn-add-doc')?.addEventListener('click', () => {
      const name = prompt('Doctor Full Name:');
      if (!name) return;
      const spec = prompt('Medical Specialty:', 'General Medicine');
      const addr = ClientCrypto.generateAddress();

      appState.hospitalDoctors.push({
        id: 'doc-' + Date.now(),
        name,
        specialty: spec || 'Specialist',
        address: addr,
        activeCases: 0
      });
      appState.notify();
      this.render();
    });
  }

  showDelegateModal(parentKeyId, parentValidUntil, recordHash) {
    const root = this.container.querySelector('#hospital-modal-root');
    const doctors = appState.hospitalDoctors;
    const now = Math.floor(Date.now() / 1000);
    const maxSecondsLeft = Math.max(0, parentValidUntil - now);

    root.innerHTML = `
      <div class="modal-overlay">
        <div class="modal-dialog">
          <div class="card-header">
            <h3 class="card-title">➡️ Delegate Temporal Key to Doctor</h3>
            <button class="btn btn-secondary btn-sm" id="btn-close-delegate">✕</button>
          </div>

          <form id="delegate-form">
            <div class="form-group">
              <label class="form-label">Select Doctor in Hospital Database</label>
              <select class="form-select" id="delegate-doctor">
                ${doctors.map(d => `
                  <option value="${d.address}">${d.name} (${d.specialty})</option>
                `).join('')}
              </select>
            </div>

            <div class="form-group">
              <label class="form-label">Delegated Duration (Seconds)</label>
              <input type="number" class="form-input" id="delegate-duration" 
                     value="${Math.min(maxSecondsLeft, 43200)}" 
                     max="${maxSecondsLeft}" min="60" required>
              <span style="font-size: 0.75rem; color: var(--text-dim); margin-top: 4px;">
                Maximum allowed: ${Math.floor(maxSecondsLeft / 3600)} hours (cannot exceed parent patient grant).
              </span>
            </div>

            <div style="background: rgba(14, 165, 233, 0.1); border: 1px solid var(--primary); padding: 0.75rem; border-radius: var(--radius-sm); font-size: 0.8rem; margin-bottom: 1rem;">
              ℹ️ The delegated key will be linked to Parent Key ID: <code>${parentKeyId.substring(0, 16)}...</code>.
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 0.75rem;">
              <button type="button" class="btn btn-secondary" id="btn-cancel-delegate">Cancel</button>
              <button type="submit" class="btn btn-primary">Sign & Delegate Key</button>
            </div>
          </form>
        </div>
      </div>
    `;

    const closeModal = () => { root.innerHTML = ''; };
    root.querySelector('#btn-close-delegate').addEventListener('click', closeModal);
    root.querySelector('#btn-cancel-delegate').addEventListener('click', closeModal);

    root.querySelector('#delegate-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const doctorAddress = root.querySelector('#delegate-doctor').value;
      const durationSeconds = parseInt(root.querySelector('#delegate-duration').value, 10);

      const parentKey = appState.temporalKeys.find(k => k.tx_id === parentKeyId);
      let payloadObj = {};
      if (parentKey) {
        try { payloadObj = JSON.parse(parentKey.payload); } catch {}
      }

      await api.delegateTemporalKey({
        sender: appState.currentUser.address,
        recipient: doctorAddress,
        parent_tx_id: parentKeyId,
        record_hash: recordHash,
        valid_from: now,
        valid_until: now + durationSeconds,
        payload: {
          doctor_recipient: doctorAddress,
          delegated_by: appState.currentUser.name,
          authorized_document_sym_key_hex: payloadObj.authorized_document_sym_key_hex
        }
      });

      closeModal();
      this.render();
    });
  }
}
