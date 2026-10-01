/**
 * Sanjeev - Blockchain Viewer (Bird's Eye Explorer)
 * Provides panoramic visualization of the ledger, block seals,
 * and a directed interactive graph tracking which Address sent a temporal key
 * to which Address, and which Address the temporal key originally belonged to.
 */

import { api } from '../services/api.js';
import { DEMO_ACCOUNTS } from '../services/state.js';

export class ExplorerView {
  constructor(container) {
    this.container = container;
    this.selectedBlock = null;
    this.selectedTx = null;
  }

  async render() {
    const data = await api.getBirdsEyeView();
    const now = data.current_time || Math.floor(Date.now() / 1000);

    this.container.innerHTML = `
      <div class="view-container">
        <!-- View Header -->
        <div class="view-header">
          <div class="view-title-group">
            <div style="display: flex; align-items: center; gap: 0.75rem;">
              <h2>🌐 Blockchain Birds-Eye Viewer</h2>
              <span class="tag tag-success">PoA Consensus Verified</span>
            </div>
            <div class="view-subtitle">
              Live Auditing &bull; Temporal Access Key Lineage &bull; Sovereign Chain Provenance
            </div>
          </div>
          <button class="btn btn-secondary btn-sm" id="btn-refresh-viewer">
            🔄 Refresh Chain State
          </button>
        </div>

        <!-- Metrics Row -->
        <div class="grid-3">
          <div class="card" style="padding: 1.25rem;">
            <div style="font-size: 0.85rem; color: var(--text-muted);">Ledger Height</div>
            <div style="font-size: 2rem; font-weight: 700; color: var(--primary); margin-top: 0.25rem;">
              ${data.chain_height} Blocks
            </div>
            <div style="font-size: 0.78rem; color: var(--text-dim); margin-top: 0.25rem;">
              Sealed by Government Authorities
            </div>
          </div>

          <div class="card" style="padding: 1.25rem;">
            <div style="font-size: 0.85rem; color: var(--text-muted);">Total Transactions</div>
            <div style="font-size: 2rem; font-weight: 700; color: var(--success); margin-top: 0.25rem;">
              ${data.total_transactions}
            </div>
            <div style="font-size: 0.78rem; color: var(--text-dim); margin-top: 0.25rem;">
              Encrypted Records & Access Grants
            </div>
          </div>

          <div class="card" style="padding: 1.25rem;">
            <div style="font-size: 0.85rem; color: var(--text-muted);">Active Temporal Keys</div>
            <div style="font-size: 2rem; font-weight: 700; color: var(--warning); margin-top: 0.25rem;">
              ${data.temporal_keys ? data.temporal_keys.filter(k => k.is_active).length : 0}
            </div>
            <div style="font-size: 0.78rem; color: var(--text-dim); margin-top: 0.25rem;">
              Time-Bound Grants Currently Valid
            </div>
          </div>
        </div>

        <!-- Interactive Birds-Eye Lineage Graph -->
        <div class="card">
          <div class="card-header">
            <div>
              <h3 class="card-title">🔍 Birds-Eye Lineage Graph: Temporal Key Routing</h3>
              <p style="font-size: 0.85rem; color: var(--text-muted); margin-top: 0.25rem;">
                Tracking which Address issued a Temporal Key, which Address received it, and who it originally belonged to.
              </p>
            </div>
          </div>

          <div class="legend-panel">
            <div class="legend-item">
              <span class="legend-dot" style="background: #38bdf8;"></span> Patient (Record Owner)
            </div>
            <div class="legend-item">
              <span class="legend-dot" style="background: #f59e0b;"></span> Hospital (Intermediate Custodian)
            </div>
            <div class="legend-item">
              <span class="legend-dot" style="background: #10b981;"></span> Doctor (Delegated Care Provider)
            </div>
            <div class="legend-item">
              <span style="display: inline-block; width: 20px; height: 3px; background: #10b981;"></span> Active Access Flow
            </div>
            <div class="legend-item">
              <span style="display: inline-block; width: 20px; height: 3px; background: #ef4444; border-top: 1px dashed red;"></span> Expired / Revoked Flow
            </div>
          </div>

          <div class="birds-eye-canvas-container" id="graph-container">
            ${this.renderSvgGraph(data)}
          </div>
        </div>

        <!-- Blocks & Transactions Inspection -->
        <div class="grid-2">
          <!-- Block Registry -->
          <div class="card">
            <div class="card-header">
              <h3 class="card-title">📦 Sealed Blocks (Proof of Authority)</h3>
              <span class="tag tag-info">Consensus Trail</span>
            </div>

            <div style="display: flex; flex-direction: column; gap: 0.75rem; max-height: 420px; overflow-y: auto;">
              ${data.blocks.map(b => `
                <div style="background: var(--bg-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); padding: 0.85rem;">
                  <div style="display: flex; justify-content: space-between; align-items: center;">
                    <strong style="color: var(--primary);">Block #${b.index}</strong>
                    <span style="font-size: 0.75rem; color: var(--text-dim);">
                      ${new Date(b.timestamp * 1000).toLocaleTimeString()}
                    </span>
                  </div>
                  <div style="font-size: 0.78rem; font-family: var(--font-mono); color: var(--text-muted); margin-top: 0.35rem; word-break: break-all;">
                    Hash: ${b.hash}
                  </div>
                  <div style="font-size: 0.78rem; color: var(--success); margin-top: 0.35rem;">
                    Sealed by: <strong>${b.authority_name || 'Ministry of Health Validator'}</strong>
                  </div>
                  <div style="font-size: 0.75rem; color: var(--text-dim); margin-top: 0.2rem;">
                    Transactions: ${b.tx_count}
                  </div>
                </div>
              `).join('')}
            </div>
          </div>

          <!-- All Temporal Keys Trail -->
          <div class="card">
            <div class="card-header">
              <h3 class="card-title">⏱️ Temporal Key Lineage Registry</h3>
              <span class="tag tag-warning">Audit Log</span>
            </div>

            <div style="display: flex; flex-direction: column; gap: 0.75rem; max-height: 420px; overflow-y: auto;">
              ${data.temporal_keys ? data.temporal_keys.map(k => {
                const hoursLeft = Math.floor(k.time_remaining_seconds / 3600);
                const minutesLeft = Math.floor((k.time_remaining_seconds % 3600) / 60);

                return `
                  <div style="background: var(--bg-primary); border: 1px solid var(--border-color); border-radius: var(--radius-sm); padding: 0.85rem;">
                    <div style="display: flex; justify-content: space-between; align-items: center;">
                      <span class="tag ${k.is_active ? 'tag-success' : 'tag-danger'}">
                        ${k.is_active ? 'ACTIVE' : (k.is_revoked ? 'REVOKED' : 'EXPIRED')}
                      </span>
                      <span class="countdown-box ${k.is_active ? '' : 'expired'}" style="font-size: 0.75rem;">
                        ${k.is_active ? `${hoursLeft}h ${minutesLeft}m left` : 'Expired'}
                      </span>
                    </div>

                    <div style="margin-top: 0.5rem; font-size: 0.8rem;">
                      <div><strong>From:</strong> <span class="address-pill">${k.sender.substring(0, 16)}...</span></div>
                      <div><strong>To:</strong> <span class="address-pill">${k.recipient.substring(0, 16)}...</span></div>
                      ${k.parent_tx_id ? `
                        <div style="color: var(--warning); margin-top: 0.2rem;">
                          ↳ Sub-Delegated from Parent Key: <code>${k.parent_tx_id.substring(0, 12)}...</code>
                        </div>
                      ` : `
                        <div style="color: var(--primary); margin-top: 0.2rem;">
                          ★ Original Master Key issued by Patient
                        </div>
                      `}
                    </div>
                  </div>
                `;
              }).join('') : '<p class="text-muted">No temporal keys on ledger.</p>'}
            </div>
          </div>
        </div>
      </div>
    `;

    this.attachEventListeners();
  }

  renderSvgGraph(data) {
    const keys = data.temporal_keys || [];
    const width = 1100;
    const height = 460;

    // Identify roles of addresses
    const patientAddr = DEMO_ACCOUNTS.patient.address;
    const hospitalAddr = DEMO_ACCOUNTS.hospital.address;
    const doctorAddr = DEMO_ACCOUNTS.doctor_rajesh.address;

    // Fixed coordinates for clean layout
    const patientPos = { x: 180, y: 220, label: 'Patient Alice (Owner)', role: 'Patient', color: '#38bdf8' };
    const hospitalPos = { x: 550, y: 150, label: 'Apollo Hospital (Facility)', role: 'Hospital', color: '#f59e0b' };
    const doctorPos = { x: 920, y: 260, label: 'Dr. Rajesh Sharma (Specialist)', role: 'Doctor', color: '#10b981' };

    // Find links
    const grant = keys.find(k => k.type === 'TEMPORAL_KEY_GRANT');
    const delegation = keys.find(k => k.type === 'TEMPORAL_KEY_DELEGATE');

    const grantActive = grant ? grant.is_active : true;
    const delActive = delegation ? delegation.is_active : true;

    return `
      <svg class="svg-graph" viewBox="0 0 ${width} ${height}" xmlns="http://www.w3.org/2000/svg">
        <defs>
          <linearGradient id="flow-active" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#38bdf8"/>
            <stop offset="100%" stop-color="#f59e0b"/>
          </linearGradient>
          <linearGradient id="flow-del" x1="0%" y1="0%" x2="100%" y2="0%">
            <stop offset="0%" stop-color="#f59e0b"/>
            <stop offset="100%" stop-color="#10b981"/>
          </linearGradient>

          <marker id="arrow-active" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="#10b981"/>
          </marker>
          <marker id="arrow-grant" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="#f59e0b"/>
          </marker>
          <marker id="arrow-expired" viewBox="0 0 10 10" refX="28" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="#ef4444"/>
          </marker>
        </defs>

        <!-- Background grid -->
        <pattern id="grid" width="40" height="40" patternUnits="userSpaceOnUse">
          <path d="M 40 0 L 0 0 0 40" fill="none" stroke="rgba(45, 59, 85, 0.25)" stroke-width="1"/>
        </pattern>
        <rect width="100%" height="100%" fill="url(#grid)" />

        <!-- Line 1: Patient -> Hospital -->
        <path d="M ${patientPos.x} ${patientPos.y} Q 365 140 ${hospitalPos.x} ${hospitalPos.y}" 
              fill="none" 
              stroke="${grantActive ? 'url(#flow-active)' : '#ef4444'}" 
              stroke-width="${grantActive ? 3.5 : 2}" 
              stroke-dasharray="${grantActive ? 'none' : '6,6'}"
              marker-end="url(#${grantActive ? 'arrow-grant' : 'arrow-expired'})" />

        <!-- Line 1 Label (Grant Badge) -->
        <rect x="290" y="145" width="165" height="32" rx="6" fill="#111827" stroke="${grantActive ? '#f59e0b' : '#ef4444'}" stroke-width="1.5"/>
        <text x="372" y="165" fill="#f8fafc" font-size="11" font-weight="600" text-anchor="middle" font-family="system-ui">
          ${grantActive ? '⏱️ Master Grant (24h Limit)' : '❌ Key Expired / Revoked'}
        </text>

        <!-- Line 2: Hospital -> Doctor (Delegation) -->
        <path d="M ${hospitalPos.x} ${hospitalPos.y} Q 735 170 ${doctorPos.x} ${doctorPos.y}" 
              fill="none" 
              stroke="${delActive ? 'url(#flow-del)' : '#ef4444'}" 
              stroke-width="${delActive ? 3.5 : 2}" 
              stroke-dasharray="${delActive ? 'none' : '6,6'}"
              marker-end="url(#${delActive ? 'arrow-active' : 'arrow-expired'})" />

        <!-- Line 2 Label (Delegation Badge) -->
        <rect x="660" y="170" width="160" height="32" rx="6" fill="#111827" stroke="${delActive ? '#10b981' : '#ef4444'}" stroke-width="1.5"/>
        <text x="740" y="190" fill="#f8fafc" font-size="11" font-weight="600" text-anchor="middle" font-family="system-ui">
          ${delActive ? '➡️ Sub-Delegated (12h Limit)' : '❌ Sub-Key Expired'}
        </text>

        <!-- Node 1: Patient Alice -->
        <g transform="translate(${patientPos.x}, ${patientPos.y})">
          <circle r="42" fill="#111827" stroke="#38bdf8" stroke-width="3" filter="drop-shadow(0 0 12px rgba(56,189,248,0.4))"/>
          <text y="-8" fill="#38bdf8" font-size="20" text-anchor="middle">👤</text>
          <text y="14" fill="#f8fafc" font-size="11" font-weight="700" text-anchor="middle" font-family="system-ui">Patient</text>
          <text y="62" fill="#38bdf8" font-size="13" font-weight="700" text-anchor="middle" font-family="system-ui">${patientPos.label}</text>
          <text y="78" fill="#94a3b8" font-size="10" font-family="monospace" text-anchor="middle">${patientAddr.substring(0, 16)}...</text>
        </g>

        <!-- Node 2: Hospital Apollo -->
        <g transform="translate(${hospitalPos.x}, ${hospitalPos.y})">
          <circle r="46" fill="#111827" stroke="#f59e0b" stroke-width="3" filter="drop-shadow(0 0 12px rgba(245,158,11,0.4))"/>
          <text y="-10" fill="#f59e0b" font-size="24" text-anchor="middle">🏥</text>
          <text y="14" fill="#f8fafc" font-size="11" font-weight="700" text-anchor="middle" font-family="system-ui">Hospital</text>
          <text y="66" fill="#f59e0b" font-size="13" font-weight="700" text-anchor="middle" font-family="system-ui">${hospitalPos.label}</text>
          <text y="82" fill="#94a3b8" font-size="10" font-family="monospace" text-anchor="middle">${hospitalAddr.substring(0, 16)}...</text>
        </g>

        <!-- Node 3: Doctor Rajesh -->
        <g transform="translate(${doctorPos.x}, ${doctorPos.y})">
          <circle r="42" fill="#111827" stroke="#10b981" stroke-width="3" filter="drop-shadow(0 0 12px rgba(16,185,129,0.4))"/>
          <text y="-8" fill="#10b981" font-size="20" text-anchor="middle">🩺</text>
          <text y="14" fill="#f8fafc" font-size="11" font-weight="700" text-anchor="middle" font-family="system-ui">Doctor</text>
          <text y="62" fill="#10b981" font-size="13" font-weight="700" text-anchor="middle" font-family="system-ui">${doctorPos.label}</text>
          <text y="78" fill="#94a3b8" font-size="10" font-family="monospace" text-anchor="middle">${doctorAddr.substring(0, 16)}...</text>
        </g>
      </svg>
    `;
  }

  attachEventListeners() {
    this.container.querySelector('#btn-refresh-viewer')?.addEventListener('click', () => {
      this.render();
    });
  }
}
