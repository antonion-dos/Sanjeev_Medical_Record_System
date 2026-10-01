/**
 * Sanjeev Settings Modal
 * Allows configuring custom blockchain node URL with live connection verification,
 * Supabase project credentials, and OpenRouter AI API keys.
 */

import { ConfigManager } from '../config/node-config.js';
import { api } from '../services/api.js';
import { appState } from '../services/state.js';

export class SettingsModal {
  static open() {
    this.close();
    const currentUrl = ConfigManager.getNodeUrl();
    const { url: sbUrl, anonKey: sbKey } = ConfigManager.getSupabaseConfig();
    const openRouterKey = appState.openRouterKey;

    const overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'settings-modal-overlay';
    overlay.innerHTML = `
      <div class="modal-dialog" style="max-width: 580px;">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 1.25rem;">
          <h3 style="margin: 0; display: flex; align-items: center; gap: 0.5rem;">
            ⚙️ Application & Node Settings
          </h3>
          <button class="btn btn-secondary btn-sm" id="btn-close-settings" style="padding: 0.25rem 0.5rem;">✕</button>
        </div>

        <!-- Section 1: Blockchain Node Configuration -->
        <div class="card" style="margin-bottom: 1rem; padding: 1rem;">
          <h4 style="margin-bottom: 0.5rem; color: var(--primary);">🌐 Sanjeev PoA Blockchain Node</h4>
          <p class="text-muted" style="font-size: 0.8rem; margin-bottom: 0.75rem;">
            Configure the embedded C++ REST node HTTP endpoint (/api/v1/chain/status).
          </p>
          <div style="display: flex; gap: 0.5rem; margin-bottom: 0.5rem;">
            <input type="text" id="setting-node-url" class="form-input" value="${currentUrl}" placeholder="http://localhost:8080" style="flex: 1;">
            <button class="btn btn-primary" id="btn-verify-node" style="white-space: nowrap;">
              Verify Connection
            </button>
          </div>
          <div id="node-verify-result" style="font-size: 0.82rem; padding: 0.5rem; border-radius: var(--radius-sm); display: none;"></div>
        </div>

        <!-- Section 2: Supabase Cloud Database -->
        <div class="card" style="margin-bottom: 1rem; padding: 1rem;">
          <h4 style="margin-bottom: 0.5rem; color: #10b981;">⚡ Supabase Auth & Cloud Records</h4>
          <p class="text-muted" style="font-size: 0.8rem; margin-bottom: 0.75rem;">
            Enter your project URL and public anon key from Supabase Dashboard -> Project Settings -> API.
          </p>
          <div style="margin-bottom: 0.5rem;">
            <label style="font-size: 0.75rem; color: var(--text-dim);">Project URL</label>
            <input type="text" id="setting-sb-url" class="form-input" value="${sbUrl}" placeholder="https://your-project.supabase.co">
          </div>
          <div>
            <label style="font-size: 0.75rem; color: var(--text-dim);">Anon Public Key</label>
            <input type="password" id="setting-sb-key" class="form-input" value="${sbKey}" placeholder="eyJhbGciOi...">
          </div>
        </div>

        <!-- Section 3: OpenRouter LLM Buddy Key -->
        <div class="card" style="margin-bottom: 1.25rem; padding: 1rem;">
          <h4 style="margin-bottom: 0.5rem; color: #8b5cf6;">🩺 OpenRouter AI Medical Buddy</h4>
          <input type="password" id="setting-or-key" class="form-input" value="${openRouterKey}" placeholder="sk-or-v1-...">
        </div>

        <div style="display: flex; justify-content: flex-end; gap: 0.75rem;">
          <button class="btn btn-secondary" id="btn-cancel-settings">Cancel</button>
          <button class="btn btn-primary" id="btn-save-settings">Save Settings</button>
        </div>
      </div>
    `;

    document.body.appendChild(overlay);
    this.bindEvents(overlay);
  }

  static bindEvents(overlay) {
    const close = () => this.close();
    overlay.querySelector('#btn-close-settings').addEventListener('click', close);
    overlay.querySelector('#btn-cancel-settings').addEventListener('click', close);

    // Verify node connection handler
    overlay.querySelector('#btn-verify-node').addEventListener('click', async () => {
      const url = overlay.querySelector('#setting-node-url').value;
      const resBox = overlay.querySelector('#node-verify-result');
      resBox.style.display = 'block';
      resBox.style.background = 'rgba(56, 189, 248, 0.1)';
      resBox.style.color = 'var(--text-main)';
      resBox.innerHTML = '<em>Testing connection to /api/v1/chain/status...</em>';

      const res = await api.verifyNodeConnection(url);
      if (res.success) {
        resBox.style.background = 'var(--success-bg)';
        resBox.style.border = '1px solid var(--success)';
        resBox.innerHTML = `
          <strong>🟢 Node Online (${res.latencyMs}ms)</strong><br>
          Authority: <code>${res.authorityName}</code> | Height: <strong>${res.chainHeight}</strong> | Mempool: <strong>${res.mempoolSize}</strong>
        `;
      } else {
        resBox.style.background = 'var(--danger-bg)';
        resBox.style.border = '1px solid var(--danger)';
        resBox.innerHTML = `<strong>🔴 Unreachable (${res.latencyMs}ms):</strong> ${res.error}<br><small>Local PoA fallback will be used.</small>`;
      }
    });

    // Save settings handler
    overlay.querySelector('#btn-save-settings').addEventListener('click', () => {
      const nodeUrl = overlay.querySelector('#setting-node-url').value;
      const sbUrl = overlay.querySelector('#setting-sb-url').value;
      const sbKey = overlay.querySelector('#setting-sb-key').value;
      const orKey = overlay.querySelector('#setting-or-key').value;

      api.setBaseUrl(nodeUrl);
      ConfigManager.setSupabaseConfig(sbUrl, sbKey);
      if (orKey !== undefined) appState.setOpenRouterKey(orKey);
      this.close();
    });
  }

  static close() {
    const existing = document.getElementById('settings-modal-overlay');
    if (existing) existing.remove();
  }
}
