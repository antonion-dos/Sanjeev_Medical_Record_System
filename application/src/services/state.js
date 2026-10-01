/**
 * Sanjeev Application State Management
 * Handles active user role, account selection, records, keys, and event broadcasting.
 */

export const DEMO_ACCOUNTS = {
  patient: {
    address: '0x71C8A3E94b15091a141b7fB7d3FaB1824cDe5601',
    name: 'Alice Sharma',
    role: 'patient',
    desc: 'Patient (Owner of Health Records)'
  },
  hospital: {
    address: '0x92BC34eD5a109aC7F59f8A43b92C78A21098E640',
    name: 'Apollo City Hospital & Medical Center',
    role: 'hospital',
    desc: 'Hospital Facility (Managing affiliated doctors and patient keys)',
    doctors: [
      {
        id: 'doc-1',
        name: 'Dr. Rajesh Sharma, MD',
        specialty: 'Cardiology Specialist',
        address: '0x38AF44cE15993a4dB2e9575fDb2b28F10815B452',
        activeCases: 1
      },
      {
        id: 'doc-2',
        name: 'Dr. Priya Patel, MS',
        specialty: 'Senior Endocrinologist',
        address: '0x64FD1129bca5988e401FcEb6Db02347201E9804B',
        activeCases: 0
      }
    ]
  },
  authority: {
    address: '0x10A98F722Bc8923a1F1388b14A0c23947b1981F4',
    name: 'Ministry of Health & Family Welfare',
    role: 'authority',
    desc: 'Institutional PoA Validator'
  }
};

DEMO_ACCOUNTS.doctor_rajesh = DEMO_ACCOUNTS.hospital.doctors[0];
DEMO_ACCOUNTS.doctor_priya = DEMO_ACCOUNTS.hospital.doctors[1];

class StateService {
  constructor() {
    this.subscribers = [];
    this.currentMode = 'patient'; // 'patient' | 'hospital' | 'explorer'
    this.currentUser = DEMO_ACCOUNTS.patient;
    this.openRouterKey = localStorage.getItem('sanjeev_openrouter_key') || '';
    this.selectedModel = localStorage.getItem('sanjeev_model') || 'anthropic/claude-3.5-sonnet';
    
    // Doctor registry under the hospital
    this.hospitalDoctors = DEMO_ACCOUNTS.hospital.doctors;

    // Local cached records and temporal keys
    this.records = [];
    this.temporalKeys = [];
    this.blocks = [];
  }

  setMode(mode) {
    this.currentMode = mode;
    if (mode === 'patient') {
      this.currentUser = DEMO_ACCOUNTS.patient;
    } else if (mode === 'hospital') {
      this.currentUser = DEMO_ACCOUNTS.hospital;
    }
    this.notify();
  }

  setUser(accountKey) {
    if (DEMO_ACCOUNTS[accountKey]) {
      this.currentUser = DEMO_ACCOUNTS[accountKey];
      this.currentMode = this.currentUser.role;
      this.notify();
    }
  }

  setOpenRouterKey(key) {
    this.openRouterKey = key.trim();
    localStorage.setItem('sanjeev_openrouter_key', this.openRouterKey);
    this.notify();
  }

  setModel(model) {
    this.selectedModel = model;
    localStorage.setItem('sanjeev_model', model);
    this.notify();
  }

  subscribe(callback) {
    this.subscribers.push(callback);
    return () => {
      this.subscribers = this.subscribers.filter(cb => cb !== callback);
    };
  }

  notify() {
    for (const cb of this.subscribers) {
      try {
        cb(this);
      } catch (err) {
        console.error('Subscriber callback error:', err);
      }
    }
  }
}

export const appState = new StateService();
