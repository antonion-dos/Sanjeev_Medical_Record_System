/**
 * Sanjeev - Patient View Component
 * Provides encrypted health records management, temporal key generation,
 * key revocation, and OpenRouter AI Medical Buddy integration.
 */

import { appState, DEMO_ACCOUNTS } from '../services/state.js';
import { api } from '../services/api.js';
import { ClientCrypto } from '../services/crypto.js';
import { OpenRouterService, POPULAR_MODELS } from '../services/openrouter.js';

export class PatientView {
  constructor(container) {
    this.container = container;
    this.activeTab = 'records'; // 'records' | 'keys' | 'buddy'
    this.decryptedRecords = {}; // cache decrypted plaintext by tx_id
    this.chatHistory = [];
    this.selectedRecordForBuddy = null;
    this.isAiLoading = false;
  }

  async render() {
    const patient = appState.currentUser;
    const records = await api.getRecords(patient.address);
    const now = Math.floor(Date.now() / 1000);

    // Filter keys issued by this patient
    const myKeys = appState.temporalKeys.filter(
      k => k.sender.toLowerCase() === patient.address.toLowerCase()
    );

    this.container.innerHTML = `
      <div class="view-container">
        <!-- View Header -->
        <div class="view-header">
          <div class="view-title-group">
            <h2>Patient Health Portal</h2>
            <div class="view-subtitle">
              Encrypted Medical Vault &bull; Zero-Knowledge Sovereign Records &bull; Address: 
              <span class="address-pill">${patient.address}</span>
            </div>
          </div>
          <div style="display: flex; gap: 0.75rem;">
            <button class="btn btn-primary" id="btn-new-record">
              <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="12" y1="5" x2="12" y2="19"></line><line x1="5" y1="12" x2="19" y2="12"></line></svg>
              Add Medical Record
            </button>
          </div>
        </div>

        <!-- Sub Tabs -->
        <div style="display: flex; gap: 0.5rem; border-bottom: 1px solid var(--border-color); padding-bottom: 0.5rem;">
          <button class="btn ${this.activeTab === 'records' ? 'btn-primary' : 'btn-secondary'} btn-sm" id="tab-records">
            My Records (${records.length})
          </button>
          <button class="btn ${this.activeTab === 'keys' ? 'btn-primary' : 'btn-secondary'} btn-sm" id="tab-keys">
            Active Temporal Keys (${myKeys.filter(k => k.valid_until > now).length})
          </button>
          <button class="btn ${this.activeTab === 'buddy' ? 'btn-primary' : 'btn-secondary'} btn-sm" id="tab-buddy">
            🩺 AI Medical Buddy
          </button>
        </div>

        <!-- Tab Contents -->
        <div id="patient-tab-content">
          ${this.renderTabContent(records, myKeys, now)}
        </div>
      </div>

      <!-- Modals Container -->
      <div id="modal-root"></div>
    `;

    this.attachEventListeners(records, myKeys);
  }

  renderTabContent(records, myKeys, now) {
    if (this.activeTab === 'records') {
      return this.renderRecordsTab(records);
    } else if (this.activeTab === 'keys') {
      return this.renderKeysTab(myKeys, now);
    } else if (this.activeTab === 'buddy') {
      return this.renderBuddyTab(records);
    }
    return '';
  }

  renderRecordsTab(records) {
    if (records.length === 0) {
      return `
        <div class="card" style="text-align: center; padding: 3rem;">
          <h3 style="margin-bottom: 0.5rem;">No Medical Records Found</h3>
          <p class="text-muted" style="margin-bottom: 1.5rem;">Upload your first confidential medical consultation or lab panel.</p>
          <div><button class="btn btn-primary" id="btn-empty-add">Upload Medical Record</button></div>
        </div>
      `;
    }

    return `
      <div class="grid-2">
        ${records.map(r => {
          let payload = {};
          try { payload = JSON.parse(r.payload); } catch {}
          const isDecrypted = !!this.decryptedRecords[r.tx_id];
          const decryptedText = this.decryptedRecords[r.tx_id] || '';

          return `
            <div class="record-card" data-txid="${r.tx_id}">
              <div style="display: flex; justify-content: space-between; align-items: flex-start;">
                <div>
                  <h3 style="font-size: 1.1rem; color: var(--text-main); font-weight: 600;">
                    ${payload.title || 'Medical Consultation Record'}
                  </h3>
                  <div class="record-meta">
                    <span>🏥 ${payload.hospital_issuer || 'Authorized Clinic'}</span>
                    <span>&bull;</span>
                    <span>🏷️ ${payload.department || 'General Practice'}</span>
                  </div>
                </div>
                <span class="tag tag-success">ENCRYPTED (AES-GCM)</span>
              </div>

              <div class="record-ciphertext" title="Encrypted on-chain payload">
                Ciphertext: ${payload.ciphertext ? payload.ciphertext.substring(0, 48) + '...' : 'SECURE_BLOB'}
              </div>

              ${isDecrypted ? `
                <div class="record-decrypted">
                  <strong>🔓 Decrypted Clinical Content:</strong>\n${decryptedText}
                </div>
              ` : ''}

              <div style="display: flex; gap: 0.5rem; margin-top: 1rem; flex-wrap: wrap;">
                <button class="btn btn-secondary btn-sm btn-decrypt" data-txid="${r.tx_id}">
                  ${isDecrypted ? 'Hide Plaintext' : '🔓 Decrypt Locally'}
                </button>
                <button class="btn btn-primary btn-sm btn-grant-key" data-txid="${r.tx_id}" data-hash="${r.record_hash}">
                  ⏱️ Issue Temporal Key
                </button>
                <button class="btn btn-secondary btn-sm btn-ask-buddy" data-txid="${r.tx_id}">
                  🤖 Ask Medical Buddy
                </button>
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;
  }

  renderKeysTab(myKeys, now) {
    if (myKeys.length === 0) {
      return `
        <div class="card" style="text-align: center; padding: 3rem;">
          <h3 style="margin-bottom: 0.5rem;">No Active Temporal Keys</h3>
          <p class="text-muted">You have not granted temporary decryption access to any hospital or doctor.</p>
        </div>
      `;
    }

    return `
      <div>
        ${myKeys.map(k => {
          let payload = {};
          try { payload = JSON.parse(k.payload); } catch {}
          const isExpired = k.valid_until > 0 && now > k.valid_until;
          const timeLeft = Math.max(0, k.valid_until - now);
          const hours = Math.floor(timeLeft / 3600);
          const minutes = Math.floor((timeLeft % 3600) / 60);

          return `
            <div class="temporal-key-item">
              <div class="key-row">
                <div>
                  <span class="tag ${isExpired ? 'tag-danger' : 'tag-success'}">
                    ${isExpired ? 'EXPIRED' : 'ACTIVE KEY'}
                  </span>
                  <strong style="margin-left: 0.5rem; font-size: 0.95rem;">
                    ${payload.grant_purpose || 'Clinical Consultation Access'}
                  </strong>
                </div>
                <div class="countdown-box ${isExpired ? 'expired' : ''}">
                  ⏱️ ${isExpired ? 'Access Expired' : `${hours}h ${minutes}m Remaining`}
                </div>
              </div>

              <div class="key-row" style="font-size: 0.85rem; color: var(--text-muted);">
                <div>Recipient: <span class="address-pill">${k.recipient}</span></div>
                <div>Key ID: <span class="address-pill">${k.tx_id.substring(0, 16)}...</span></div>
              </div>

              <div class="key-row" style="margin-top: 0.25rem;">
                <div style="font-size: 0.8rem; color: var(--text-dim);">
                  Valid Until: ${new Date(k.valid_until * 1000).toLocaleString()}
                </div>
                ${!isExpired ? `
                  <button class="btn btn-danger btn-sm btn-revoke-key" data-keyid="${k.tx_id}">
                    Revoke Key Now
                  </button>
                ` : ''}
              </div>
            </div>
          `;
        }).join('')}
      </div>
    `;
  }

  renderBuddyTab(records) {
    return `
      <div class="grid-2">
        <!-- Settings & Record Selector -->
        <div class="card">
          <div class="card-header">
            <h3 class="card-title">🩺 Medical Buddy Settings</h3>
            <span class="tag tag-info">OpenRouter AI</span>
          </div>

          <div class="form-group">
            <label class="form-label">Select Medical Record to Decrypt & Analyze</label>
            <select class="form-select" id="buddy-record-select">
              ${records.map(r => {
                let p = {};
                try { p = JSON.parse(r.payload); } catch {}
                const selected = this.selectedRecordForBuddy === r.tx_id ? 'selected' : '';
                return `<option value="${r.tx_id}" ${selected}>${p.title || 'Record'} (${r.tx_id.substring(0, 10)})</option>`;
              }).join('')}
            </select>
          </div>

          <div class="form-group">
            <label class="form-label">AI Model (OpenRouter)</label>
            <select class="form-select" id="buddy-model-select">
              ${POPULAR_MODELS.map(m => `
                <option value="${m.id}" ${appState.selectedModel === m.id ? 'selected' : ''}>
                  ${m.name}
                </option>
              `).join('')}
            </select>
          </div>

          <div class="form-group">
            <label class="form-label">OpenRouter API Key (Optional)</label>
            <input type="password" class="form-input" id="buddy-api-key" 
                   placeholder="sk-or-v1-..." value="${appState.openRouterKey}">
            <span style="font-size: 0.75rem; color: var(--text-dim); margin-top: 4px;">
              Your key is saved locally in browser storage. If omitted, built-in clinical simulation is used.
            </span>
          </div>

          <div style="margin-top: 1rem;">
            <button class="btn btn-primary" id="btn-start-buddy" style="width: 100%;">
              🔓 Decrypt & Interpret Case File with AI
            </button>
          </div>
        </div>

        <!-- Chat / Interpretation Conversation -->
        <div class="card">
          <div class="card-header">
            <h3 class="card-title">💬 Clinical Companion Chat</h3>
            <span class="tag tag-success">Client-Side Secure</span>
          </div>

          <div class="buddy-chat-container">
            <div class="chat-history" id="chat-history-box">
              ${this.chatHistory.length === 0 ? `
                <div class="chat-bubble system">
                  🔒 Zero-Knowledge Privacy: Your records are decrypted locally in your browser memory before sending to your selected OpenRouter model.
                </div>
                <div class="chat-bubble assistant">
                  Hello Alice! Select your medical record on the left and click "Decrypt & Interpret Case File with AI" to get a plain-English explanation, prescription guide, and questions for your doctor.
                </div>
              ` : this.chatHistory.map(m => {
                const safe = String(m.content)
                  .replace(/&/g, '&amp;')
                  .replace(/</g, '&lt;')
                  .replace(/>/g, '&gt;')
                  .replace(/\n/g, '<br>');
                return `<div class="chat-bubble ${m.role}">${safe}</div>`;
              }).join('')}
              ${this.isAiLoading ? `
                <div class="chat-bubble assistant" style="display: flex; align-items: center; gap: 0.5rem;">
                  <span class="status-dot" style="animation: pulse 1s infinite;"></span>
                  Consulting AI Medical Buddy...
                </div>
              ` : ''}
            </div>

            <div class="chat-input-bar">
              <input type="text" class="form-input" id="chat-input-text" 
                     placeholder="Ask about your diagnosis, medicines, or symptoms..." 
                     style="flex: 1;">
              <button class="btn btn-primary" id="btn-send-chat">Send</button>
            </div>
          </div>
        </div>
      </div>
    `;
  }

  attachEventListeners(records, myKeys) {
    // Tab switching
    this.container.querySelector('#tab-records')?.addEventListener('click', () => {
      this.activeTab = 'records';
      this.render();
    });
    this.container.querySelector('#tab-keys')?.addEventListener('click', () => {
      this.activeTab = 'keys';
      this.render();
    });
    this.container.querySelector('#tab-buddy')?.addEventListener('click', () => {
      this.activeTab = 'buddy';
      this.render();
    });

    // New record buttons
    this.container.querySelector('#btn-new-record')?.addEventListener('click', () => this.showNewRecordModal());
    this.container.querySelector('#btn-empty-add')?.addEventListener('click', () => this.showNewRecordModal());

    // Decrypt locally
    this.container.querySelectorAll('.btn-decrypt').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        const txId = e.currentTarget.dataset.txid;
        if (this.decryptedRecords[txId]) {
          delete this.decryptedRecords[txId];
        } else {
          const rec = records.find(r => r.tx_id === txId);
          if (rec) {
            let p = JSON.parse(rec.payload);
            try {
              const key = await ClientCrypto.importKeyHex(p.document_sym_key_hex);
              const plain = await ClientCrypto.decrypt(p, key);
              this.decryptedRecords[txId] = plain;
            } catch (err) {
              alert('Decryption failed: ' + err.message);
            }
          }
        }
        this.render();
      });
    });

    // Issue Temporal Key
    this.container.querySelectorAll('.btn-grant-key').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const txId = e.currentTarget.dataset.txid;
        const hash = e.currentTarget.dataset.hash;
        this.showGrantKeyModal(txId, hash);
      });
    });

    // Ask Medical Buddy
    this.container.querySelectorAll('.btn-ask-buddy').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const txId = e.currentTarget.dataset.txid;
        this.selectedRecordForBuddy = txId;
        this.activeTab = 'buddy';
        this.render();
      });
    });

    // Revoke Key
    this.container.querySelectorAll('.btn-revoke-key').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        const keyId = e.currentTarget.dataset.keyid;
        if (confirm('Are you sure you want to revoke this temporal key immediately?')) {
          await api.revokeTemporalKey(keyId);
          this.render();
        }
      });
    });

    // OpenRouter Settings
    const keyInput = this.container.querySelector('#buddy-api-key');
    if (keyInput) {
      keyInput.addEventListener('change', (e) => {
        appState.setOpenRouterKey(e.target.value);
      });
    }

    const modelSelect = this.container.querySelector('#buddy-model-select');
    if (modelSelect) {
      modelSelect.addEventListener('change', (e) => {
        appState.setModel(e.target.value);
      });
    }

    // Start Buddy Interpretation
    const startBuddyBtn = this.container.querySelector('#btn-start-buddy');
    if (startBuddyBtn) {
      startBuddyBtn.addEventListener('click', async () => {
        // Capture ALL inputs BEFORE any await (DOM may change after)
        const recordSelect = this.container.querySelector('#buddy-record-select');
        const txId = recordSelect ? recordSelect.value : null;
        if (!txId) { alert('Please select a record first.'); return; }

        const keyInput = this.container.querySelector('#buddy-api-key');
        const liveKey = keyInput ? keyInput.value.replace(/[^\x20-\x7E]/g, '').trim() : '';
        if (liveKey) appState.setOpenRouterKey(liveKey);
        const apiKey = liveKey || appState.openRouterKey;

        const selectedModel = this.container.querySelector('#buddy-model-select')?.value || appState.selectedModel;
        if (selectedModel) appState.setModel(selectedModel);

        const rec = records.find(r => r.tx_id === txId);
        if (!rec) return;

        // Show loading directly in the chat box without a full re-render
        const chatBox = this.container.querySelector('#chat-history-box');
        const loadingBubble = document.createElement('div');
        loadingBubble.className = 'chat-bubble assistant';
        loadingBubble.id = 'loading-bubble';
        loadingBubble.innerHTML = '<span class="status-dot" style="animation:pulse 1s infinite;margin-right:6px;"></span>Consulting AI Medical Buddy...';
        if (chatBox) chatBox.appendChild(loadingBubble);
        startBuddyBtn.disabled = true;

        try {
          const p = JSON.parse(rec.payload);
          let plainText = '';
          try {
            const key = await ClientCrypto.importKeyHex(p.document_sym_key_hex);
            plainText = await ClientCrypto.decrypt(p, key);
          } catch (cryptoErr) {
            throw new Error(`Record decryption failed: ${cryptoErr.message}`);
          }

          const aiResponse = await OpenRouterService.interpretMedicalRecord({
            apiKey,
            model: appState.selectedModel,
            decryptedText: plainText
          });

          this.chatHistory.push({ role: 'assistant', content: aiResponse });
        } catch (err) {
          const msg = err.message.startsWith('Record decryption')
            ? `⚠️ ${err.message}`
            : `⚠️ Failed to contact OpenRouter: ${err.message}. Check your API key and model access.`;
          this.chatHistory.push({
            role: 'assistant',
            content: msg
          });
        } finally {
          startBuddyBtn.disabled = false;
          this.isAiLoading = false;
          this.render();
        }
      });
    }

    // Conversational Chat Send
    const sendBtn = this.container.querySelector('#btn-send-chat');
    const chatInput = this.container.querySelector('#chat-input-text');
    if (sendBtn && chatInput) {
      const handleSend = async () => {
        const query = chatInput.value.trim();
        if (!query) return;

        // Capture all inputs before any await
        const keyInput = this.container.querySelector('#buddy-api-key');
        const liveKey = keyInput ? keyInput.value.replace(/[^\x20-\x7E]/g, '').trim() : '';
        if (liveKey) appState.setOpenRouterKey(liveKey);
        const apiKey = liveKey || appState.openRouterKey;

        const recordSelect = this.container.querySelector('#buddy-record-select');
        const txId = recordSelect ? recordSelect.value : (records[0]?.tx_id);

        this.chatHistory.push({ role: 'user', content: query });
        chatInput.value = '';
        sendBtn.disabled = true;

        // Show loading bubble directly without full re-render
        const chatBox = this.container.querySelector('#chat-history-box');
        const loadingBubble = document.createElement('div');
        loadingBubble.className = 'chat-bubble assistant';
        loadingBubble.id = 'loading-bubble-chat';
        loadingBubble.innerHTML = '<span class="status-dot" style="animation:pulse 1s infinite;margin-right:6px;"></span>Thinking...';
        if (chatBox) chatBox.appendChild(loadingBubble);

        // Also show user message immediately
        const userBubble = document.createElement('div');
        userBubble.className = 'chat-bubble user';
        userBubble.textContent = query;
        if (chatBox && loadingBubble) chatBox.insertBefore(userBubble, loadingBubble);

        try {
          let plainText = '';
          if (txId) {
            const rec = records.find(r => r.tx_id === txId);
            if (rec) {
              try {
                const p = JSON.parse(rec.payload);
                const key = await ClientCrypto.importKeyHex(p.document_sym_key_hex);
                plainText = await ClientCrypto.decrypt(p, key);
              } catch (cryptoErr) {
                console.warn('Could not decrypt record for chat context:', cryptoErr);
              }
            }
          }

          const aiReply = await OpenRouterService.interpretMedicalRecord({
            apiKey,
            model: appState.selectedModel,
            decryptedText: plainText,
            userQuestion: query,
            chatHistory: this.chatHistory.slice(0, -1) // exclude the user msg we just pushed
          });

          this.chatHistory.push({ role: 'assistant', content: aiReply });
        } catch (err) {
          this.chatHistory.push({ role: 'assistant', content: `⚠️ Error: ${err.message}` });
        } finally {
          sendBtn.disabled = false;
          this.isAiLoading = false;
          this.render();
        }
      };

      sendBtn.addEventListener('click', handleSend);
      chatInput.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') handleSend();
      });
    }
  }

  showNewRecordModal() {
    const root = this.container.querySelector('#modal-root');
    root.innerHTML = `
      <div class="modal-overlay" id="modal-overlay">
        <div class="modal-dialog">
          <div class="card-header">
            <h3 class="card-title">🔐 Add Encrypted Medical Record</h3>
            <button class="btn btn-secondary btn-sm" id="btn-close-modal">✕</button>
          </div>

          <form id="new-record-form">
            <div class="form-group">
              <label class="form-label">Record Title</label>
              <input type="text" class="form-input" id="rec-title" required placeholder="e.g. Endocrine Consultation & HbA1c Panel">
            </div>

            <div class="grid-2">
              <div class="form-group">
                <label class="form-label">Hospital / Clinic Issuer</label>
                <input type="text" class="form-input" id="rec-hospital" value="Apollo City Hospital">
              </div>
              <div class="form-group">
                <label class="form-label">Medical Department</label>
                <input type="text" class="form-input" id="rec-dept" value="Endocrinology">
              </div>
            </div>

            <div class="form-group">
              <label class="form-label">Clinical Notes, Lab Values & Prescriptions (Encrypted Client-Side)</label>
              <textarea class="form-textarea" id="rec-body" required rows="6" placeholder="PATIENT: Alice Sharma..."></textarea>
            </div>

            <div style="background: rgba(14, 165, 233, 0.1); border: 1px solid var(--primary); padding: 0.75rem; border-radius: var(--radius-sm); font-size: 0.8rem; margin-bottom: 1rem;">
              🔒 A unique 256-bit AES-GCM symmetric key will be generated locally. Only ciphertexts will touch the blockchain ledger.
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 0.75rem;">
              <button type="button" class="btn btn-secondary" id="btn-cancel-modal">Cancel</button>
              <button type="submit" class="btn btn-primary" id="btn-submit-record">Encrypt & Store on Blockchain</button>
            </div>
          </form>
        </div>
      </div>
    `;

    const closeModal = () => { root.innerHTML = ''; };
    root.querySelector('#btn-close-modal').addEventListener('click', closeModal);
    root.querySelector('#btn-cancel-modal').addEventListener('click', closeModal);

    root.querySelector('#new-record-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const title = root.querySelector('#rec-title').value.trim();
      const hospital = root.querySelector('#rec-hospital').value.trim();
      const dept = root.querySelector('#rec-dept').value.trim();
      const body = root.querySelector('#rec-body').value.trim();

      const docKey = await ClientCrypto.generateKey();
      const docKeyHex = await ClientCrypto.exportKeyHex(docKey);
      const encData = await ClientCrypto.encrypt(body, docKey);

      const payload = {
        title,
        patient_name: appState.currentUser.name,
        department: dept,
        hospital_issuer: hospital,
        ciphertext: encData.ciphertext_b64,
        iv: encData.iv_b64,
        tag: encData.tag_b64,
        algorithm: 'AES-256-GCM',
        document_sym_key_hex: docKeyHex
      };

      await api.submitRecord(payload, appState.currentUser.address);
      closeModal();
      this.render();
    });
  }

  showGrantKeyModal(txId, recordHash) {
    const root = this.container.querySelector('#modal-root');
    root.innerHTML = `
      <div class="modal-overlay">
        <div class="modal-dialog">
          <div class="card-header">
            <h3 class="card-title">⏱️ Issue Time-Limited Temporal Key</h3>
            <button class="btn btn-secondary btn-sm" id="btn-close-grant">✕</button>
          </div>

          <form id="grant-key-form">
            <div class="form-group">
              <label class="form-label">Authorized Recipient</label>
              <select class="form-select" id="grant-recipient">
                <option value="${DEMO_ACCOUNTS.hospital.address}">
                  Apollo City Hospital (${DEMO_ACCOUNTS.hospital.address.substring(0, 16)}...)
                </option>
                <option value="${DEMO_ACCOUNTS.doctor_rajesh.address}">
                  Dr. Rajesh Sharma, MD (${DEMO_ACCOUNTS.doctor_rajesh.address.substring(0, 16)}...)
                </option>
                <option value="${DEMO_ACCOUNTS.doctor_priya.address}">
                  Dr. Priya Patel, MS (${DEMO_ACCOUNTS.doctor_priya.address.substring(0, 16)}...)
                </option>
              </select>
            </div>

            <div class="form-group">
              <label class="form-label">Access Duration (Fixed Time Limit)</label>
              <select class="form-select" id="grant-duration">
                <option value="3600">1 Hour (Urgent Consultation)</option>
                <option value="21600">6 Hours (Emergency Room Episode)</option>
                <option value="86400" selected>24 Hours (Standard Clinic Visit)</option>
                <option value="259200">3 Days (Hospital Inpatient Observation)</option>
                <option value="604800">7 Days (Post-Operative Recovery)</option>
              </select>
            </div>

            <div class="form-group">
              <label class="form-label">Clinical Purpose / Scope</label>
              <input type="text" class="form-input" id="grant-purpose" required value="Specialist Outpatient Consultation">
            </div>

            <div style="background: rgba(245, 158, 11, 0.1); border: 1px solid var(--warning); padding: 0.75rem; border-radius: var(--radius-sm); font-size: 0.8rem; margin-bottom: 1rem;">
              ⚠️ When the time limit expires, the blockchain validator and cryptographic access checks will immediately deny access.
            </div>

            <div style="display: flex; justify-content: flex-end; gap: 0.75rem;">
              <button type="button" class="btn btn-secondary" id="btn-cancel-grant">Cancel</button>
              <button type="submit" class="btn btn-primary">Sign & Issue Temporal Key</button>
            </div>
          </form>
        </div>
      </div>
    `;

    const closeModal = () => { root.innerHTML = ''; };
    root.querySelector('#btn-close-grant').addEventListener('click', closeModal);
    root.querySelector('#btn-cancel-grant').addEventListener('click', closeModal);

    root.querySelector('#grant-key-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const recipient = root.querySelector('#grant-recipient').value;
      const durationSeconds = parseInt(root.querySelector('#grant-duration').value, 10);
      const purpose = root.querySelector('#grant-purpose').value.trim();

      const records = await api.getRecords();
      const targetRecord = records.find(r => r.tx_id === txId);
      let docKeyHex = '';
      if (targetRecord) {
        const p = JSON.parse(targetRecord.payload);
        docKeyHex = p.document_sym_key_hex;
      }

      const now = Math.floor(Date.now() / 1000);
      await api.grantTemporalKey({
        sender: appState.currentUser.address,
        recipient,
        record_hash: recordHash,
        valid_from: now,
        valid_until: now + durationSeconds,
        payload: {
          grant_purpose: purpose,
          authorized_document_sym_key_hex: docKeyHex
        }
      });

      closeModal();
      this.activeTab = 'keys';
      this.render();
    });
  }
}
