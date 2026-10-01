/**
 * Sanjeev Application Core Entrypoint
 * Framework-free, modular Single Page Application.
 */

import { appState, DEMO_ACCOUNTS } from './services/state.js';
import { api } from './services/api.js';
import { PatientView } from './views/patient.js';
import { HospitalView } from './views/hospital.js';
import { ExplorerView } from './views/explorer.js';

class SanjeevApp {
  constructor() {
    this.mountPoint = document.getElementById('app-root');
    this.views = {
      patient: new PatientView(this.mountPoint),
      hospital: new HospitalView(this.mountPoint),
      explorer: new ExplorerView(this.mountPoint)
    };
  }

  async init() {
    this.renderHeader();
    this.bindEvents();

    // Check node connectivity
    const online = await api.checkConnection();
    this.updateNodeStatus(online);

    // Initial view render
    this.renderCurrentView();

    // Listen for state changes
    appState.subscribe(() => {
      this.updateHeaderState();
      this.renderCurrentView();
    });
  }

  renderHeader() {
    const header = document.getElementById('header-root');
    header.innerHTML = `
      <header class="app-header">
        <div class="header-container">
          <!-- Logo & Brand -->
          <a href="#" class="logo-section" id="brand-logo">
            <img src="./favicon.svg" alt="Sanjeev Logo" class="logo-img">
            <div>
              <div style="display: flex; align-items: center; gap: 0.5rem;">
                <span class="logo-title">Sanjeev</span>
                <span class="logo-badge">संजीव v1.0</span>
              </div>
              <div style="font-size: 0.72rem; color: var(--text-dim);">
                Decentralized Health Records &bull; Medical Buddy
              </div>
            </div>
          </a>

          <!-- Role / Mode Switcher -->
          <nav class="mode-nav">
            <button class="mode-btn ${appState.currentMode === 'patient' ? 'active' : ''}" data-mode="patient">
              👤 Patient Mode
            </button>
            <button class="mode-btn ${appState.currentMode === 'hospital' ? 'active' : ''}" data-mode="hospital">
              🏥 Hospital / Doctor Mode
            </button>
            <button class="mode-btn ${appState.currentMode === 'explorer' ? 'active' : ''}" data-mode="explorer">
              🌐 Birds-Eye Viewer
            </button>
          </nav>

          <!-- Current Account Selector & Node Status -->
          <div class="user-status">
            <div class="status-badge" id="node-status-badge" title="Blockchain Node Connection">
              <span class="status-dot" id="node-status-dot"></span>
              <span id="node-status-text">Checking Node...</span>
            </div>

            <select class="account-selector" id="account-dropdown" title="Simulated Wallet Identity">
              <option value="patient" ${appState.currentUser === DEMO_ACCOUNTS.patient ? 'selected' : ''}>
                👤 Alice Sharma (Patient)
              </option>
              <option value="hospital" ${appState.currentUser === DEMO_ACCOUNTS.hospital ? 'selected' : ''}>
                🏥 Apollo Hospital (Enterprise)
              </option>
              <option value="doctor_rajesh" ${appState.currentUser === DEMO_ACCOUNTS.doctor_rajesh ? 'selected' : ''}>
                🩺 Dr. Rajesh Sharma (Doctor)
              </option>
              <option value="doctor_priya" ${appState.currentUser === DEMO_ACCOUNTS.doctor_priya ? 'selected' : ''}>
                🩺 Dr. Priya Patel (Doctor)
              </option>
            </select>
          </div>
        </div>
      </header>
    `;
  }

  updateHeaderState() {
    document.querySelectorAll('.mode-btn').forEach(btn => {
      if (btn.dataset.mode === appState.currentMode) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });

    const dropdown = document.getElementById('account-dropdown');
    if (dropdown) {
      for (const [key, acc] of Object.entries(DEMO_ACCOUNTS)) {
        if (acc.address.toLowerCase() === appState.currentUser.address.toLowerCase()) {
          dropdown.value = key;
          break;
        }
      }
    }
  }

  updateNodeStatus(online) {
    const dot = document.getElementById('node-status-dot');
    const text = document.getElementById('node-status-text');
    if (dot && text) {
      if (online) {
        dot.style.backgroundColor = 'var(--success)';
        dot.style.boxShadow = '0 0 8px var(--success)';
        text.innerText = 'Node: Online (8080)';
      } else {
        dot.style.backgroundColor = 'var(--warning)';
        dot.style.boxShadow = '0 0 8px var(--warning)';
        text.innerText = 'Local PoA Simulation';
      }
    }
  }

  bindEvents() {
    document.addEventListener('click', (e) => {
      const modeBtn = e.target.closest('.mode-btn');
      if (modeBtn) {
        const mode = modeBtn.dataset.mode;
        appState.setMode(mode);
      }

      if (e.target.closest('#brand-logo')) {
        e.preventDefault();
        appState.setMode('patient');
      }
    });

    document.addEventListener('change', (e) => {
      if (e.target.id === 'account-dropdown') {
        appState.setUser(e.target.value);
      }
    });
  }

  renderCurrentView() {
    const currentView = this.views[appState.currentMode];
    if (currentView) {
      currentView.render();
    }
  }
}

// Bootstrap once DOM is ready
window.addEventListener('DOMContentLoaded', () => {
  const app = new SanjeevApp();
  app.init();
});
