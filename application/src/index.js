/**
 * Sanjeev Application Core Entrypoint
 * Framework-free, modular Single Page Application.
 */

import { appState, DEMO_ACCOUNTS } from './services/state.js';
import { api } from './services/api.js';
import { PatientView } from './views/patient.js';
import { HospitalView } from './views/hospital.js';
import { ExplorerView } from './views/explorer.js';
import { SettingsModal } from './views/settings-modal.js';
import { AuthModal as SupabaseAuthModal } from './views/auth-modal.js';
import { auth } from './services/auth.js';
import { AuthModal } from './components/AuthModal.js';

const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// Which header modes each signed-in role may open
const ALLOWED_MODES = { patient: ['patient', 'explorer'], doctor: ['hospital', 'explorer'], hospital: ['hospital', 'explorer'] };
const HOME_MODE = { patient: 'patient', doctor: 'hospital', hospital: 'hospital' };

class SanjeevApp {
  constructor() {
    this.mountPoint = document.getElementById('app-root');
    this.views = {
      patient: new PatientView(this.mountPoint),
      hospital: new HospitalView(this.mountPoint),
      explorer: new ExplorerView(this.mountPoint)
    };
    this.session = null;
    // One separate call per role. Fill in the doctor / hospital ones as the views grow.
    this.authModal = new AuthModal({
      auth,
      dismissible: false,
      handlers: {
        patient:  s => this.onPatientAuthenticated(s),
        doctor:   s => this.onDoctorAuthenticated(s),
        hospital: s => this.onHospitalAuthenticated(s)
      }
    });
  }

  /* ---------------- auth: one entry point per role ---------------- */

  onPatientAuthenticated(session) {
    this.applySession(session);
  }

  onDoctorAuthenticated(session) {
    // TODO(doctor view): doctor-specific bootstrapping goes here
    // (e.g. load keys delegated to session.address, pre-select this doctor in the roster).
    this.applySession(session);
  }

  onHospitalAuthenticated(session) {
    // TODO(hospital view): hospital-specific bootstrapping goes here
    // (e.g. load this hospital's team and incoming temporal keys).
    this.applySession(session);
  }

  /** Shared plumbing: make appState reflect the signed-in identity. */
  applySession(session) {
    this.session = session;
    appState.currentUser = this.resolveUser(session);
    appState.currentMode = HOME_MODE[session.role];
    this.renderSessionChip();
    appState.notify();
  }

  resolveUser(session) {
    // Demo accounts reuse the exact DEMO_ACCOUNTS objects so existing views keep working.
    const demo = Object.values(DEMO_ACCOUNTS).find(a => a.address.toLowerCase() === session.address.toLowerCase());
    if (demo) return demo;
    return {
      address: session.address,
      name: session.name,
      role: session.role,
      specialty: session.specialty,
      hospital: session.hospital?.name,
      desc: session.role === 'patient' ? 'Patient (Owner of Health Records)' : session.role === 'doctor' ? 'Doctor' : 'Healthcare Facility'
    };
  }

  startSession(session) {
    ({ patient: s => this.onPatientAuthenticated(s), doctor: s => this.onDoctorAuthenticated(s), hospital: s => this.onHospitalAuthenticated(s) })[session.role]?.(session);
  }

  switchMode(mode) {
    if (!this.session || !ALLOWED_MODES[this.session.role].includes(mode)) return;
    appState.currentMode = mode;      // keep the signed-in identity (setMode() would reset it)
    appState.notify();
  }

  renderSessionChip() {
    const slot = document.getElementById('account-dropdown') || document.getElementById('session-chip');
    if (!slot || !this.session) return;
    const s = this.session, label = { patient: 'Patient', doctor: 'Doctor', hospital: 'Hospital' }[s.role];
    const chip = document.createElement('div');
    chip.id = 'session-chip'; chip.className = 'session-chip';
    chip.innerHTML = `
      <div>
        <div class="session-chip__name">${esc(s.name)}</div>
        <div class="session-chip__meta">${label} &bull; ${esc(s.idMasked)}</div>
      </div>
      <button class="btn btn-secondary btn-sm" id="logout-btn" type="button">Log out</button>`;
    slot.replaceWith(chip);
  }

  async init() {
    this.renderHeader();
    this.bindEvents();

    // Check node connectivity
    const online = await api.checkConnection();
    this.updateNodeStatus(online);

    // Listen for state changes
    appState.subscribe(() => {
      this.updateHeaderState();
      this.renderCurrentView();
    });

    // Seed demo logins (mapped to DEMO_ACCOUNTS addresses), then require sign-in.
    // Nothing is rendered until a session exists, so no records show behind the popup.
    await auth.seedDemo({
      patient: DEMO_ACCOUNTS.patient,
      hospital: DEMO_ACCOUNTS.hospital,
      doctors: [
        { docId: 'MMC-2011-48213', address: DEMO_ACCOUNTS.doctor_rajesh.address },
        { docId: 'MMC-2014-55102', address: DEMO_ACCOUNTS.doctor_priya.address }
      ]
    });
    const existing = auth.getSession();
    if (existing) this.startSession(existing);
    else this.authModal.open();
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

          <!-- Role / Mode Switcher: STRICTLY Patient Mode and Hospital Mode -->
          <nav class="mode-nav">
            <button class="mode-btn ${appState.currentMode === 'patient' ? 'active' : ''}" data-mode="patient">
              👤 Patient Mode
            </button>
            <button class="mode-btn ${appState.currentMode === 'hospital' ? 'active' : ''}" data-mode="hospital">
              🏥 Hospital Mode
            </button>
            <button class="mode-btn ${appState.currentMode === 'explorer' ? 'active' : ''}" data-mode="explorer">
              🌐 Birds-Eye Viewer
            </button>
          </nav>

          <!-- Current Account Selector & Node Status -->
          <div class="user-status" style="display: flex; align-items: center; gap: 0.5rem;">
            <div class="status-badge" id="node-status-badge" title="Blockchain Node Connection">
              <span class="status-dot" id="node-status-dot"></span>
              <span id="node-status-text">Checking Node...</span>
            </div>

            <button class="btn btn-secondary btn-sm" id="btn-header-auth" title="Authentication & Keypair Link">
              🔐 <span id="header-auth-label">${appState.currentUser.email ? appState.currentUser.name : 'Sign In'}</span>
            </button>

            <button class="btn btn-secondary btn-sm" id="btn-header-settings" title="Application & Node Settings">
              ⚙️
            </button>

            <select class="account-selector" id="account-dropdown" title="Demo Switcher">
              <option value="patient" ${appState.currentUser === DEMO_ACCOUNTS.patient ? 'selected' : ''}>
                👤 Alice Sharma (Demo)
              </option>
              <option value="hospital" ${appState.currentUser === DEMO_ACCOUNTS.hospital ? 'selected' : ''}>
                🏥 Apollo Hospital (Demo)
              </option>
            </select>
          </div>
        </div>
      </header>
    `;
  }

  updateHeaderState() {
    document.querySelectorAll('.mode-btn').forEach(btn => {
      btn.hidden = !!this.session && !ALLOWED_MODES[this.session.role].includes(btn.dataset.mode);
      if (btn.dataset.mode === appState.currentMode) {
        btn.classList.add('active');
      } else {
        btn.classList.remove('active');
      }
    });

    const authLabel = document.getElementById('header-auth-label');
    if (authLabel) {
      authLabel.innerText = appState.currentUser.email 
        ? `${appState.currentUser.name} (${appState.currentUser.address.substring(0, 6)}...)` 
        : 'Sign In';
    }

    const dropdown = document.getElementById('account-dropdown');
    if (dropdown && appState.currentUser) {
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
        this.switchMode(modeBtn.dataset.mode);
      }

      if (e.target.closest('#brand-logo')) {
        e.preventDefault();
        if (this.session) this.switchMode(HOME_MODE[this.session.role]);
      }

      if (e.target.closest('#logout-btn')) {
        auth.logout();
        window.location.reload();
      }

      if (e.target.closest('#btn-header-settings')) {
        SettingsModal.open();
      }

      if (e.target.closest('#btn-header-auth')) {
<<<<<<< HEAD
        this.authModal.open();
=======
        SupabaseAuthModal.open();
>>>>>>> origin/main
      }
    });

    document.addEventListener('change', (e) => {
      if (e.target.id === 'account-dropdown') {
        appState.setUser(e.target.value);
      }
    });
  }

  renderCurrentView() {
    if (!this.session) return;   // signed out: render nothing behind the login popup
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
