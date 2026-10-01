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
    name: 'Apollo City Hospital & Research Center',
    role: 'hospital',
    desc: 'Healthcare Facility & Doctor Network'
  },
  doctor_rajesh: {
    address: '0x38AF44cE15993a4dB2e9575fDb2b28F10815B452',
    name: 'Dr. Rajesh Sharma, MD',
    role: 'doctor',
    specialty: 'Cardiology Specialist',
    hospital: 'Apollo City Hospital',
    desc: 'Attending Cardiologist'
  },
  doctor_priya: {
    address: '0x64FD1129bca5988e401FcEb6Db02347201E9804B',
    name: 'Dr. Priya Patel, MS',
    role: 'doctor',
    specialty: 'Endocrinology & Internal Medicine',
    hospital: 'Apollo City Hospital',
    desc: 'Senior Endocrinologist'
  },
  authority: {
    address: '0x10A98F722Bc8923a1F1388b14A0c23947b1981F4',
    name: 'Ministry of Health & Family Welfare',
    role: 'authority',
    desc: 'Institutional PoA Validator'
  }
};

class StateService {
  constructor() {
    this.subscribers = [];
    this.currentMode = 'patient'; // 'patient' | 'hospital' | 'explorer'
    this.currentUser = DEMO_ACCOUNTS.patient;
    this.openRouterKey = (localStorage.getItem('sanjeev_openrouter_key') || '').replace(/[^\x20-\x7E]/g, '').trim();
    const savedModel = localStorage.getItem('sanjeev_model');
    this.selectedModel = (savedModel && savedModel !== 'anthropic/claude-3.5-sonnet' && savedModel !== 'meta-llama/llama-3.3-70b-instruct:free')
      ? savedModel
      : 'inclusionai/ling-3.0-flash-sante:free';
    
    // Doctor registry under the hospital
    this.hospitalDoctors = [
      {
        id: 'doc-1',
        name: 'Dr. Rajesh Sharma, MD',
        specialty: 'Cardiology',
        address: DEMO_ACCOUNTS.doctor_rajesh.address,
        activeCases: 1
      },
      {
        id: 'doc-2',
        name: 'Dr. Priya Patel, MS',
        specialty: 'Endocrinology',
        address: DEMO_ACCOUNTS.doctor_priya.address,
        activeCases: 0
      }
    ];

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
      if (this.currentUser.role === 'patient') {
        this.currentMode = 'patient';
      } else if (this.currentUser.role === 'hospital' || this.currentUser.role === 'doctor') {
        this.currentMode = 'hospital';
      }
      this.notify();
    }
  }

  setOpenRouterKey(key) {
    this.openRouterKey = (key || '').replace(/[^\x20-\x7E]/g, '').trim();
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
