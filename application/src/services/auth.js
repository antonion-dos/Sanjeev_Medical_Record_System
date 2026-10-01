/**
 * Sanjeev Auth Service (demo-grade, application layer only)
 * ---------------------------------------------------------------
 * Accounts live in localStorage; OTPs are generated on-device and
 * "broadcast" via an event (AuthModal renders it as a fake SMS).
 * Nothing here touches the blockchain ledger; these are app-level identities.
 * Swap the spots marked "PRODUCTION SWAP" for a real UIDAI/SMS gateway and
 * server-side sessions when going live.
 *
 * Identities
 *   patient  -> Aadhaar number (12 digits, Verhoeff checked) + linked mobile, OTP login
 *   hospital -> Hospital registration number (must be verified in the admin registry), OTP login
 *   doctor   -> Doctor registration ID + password. Name, specialty and hospital are
 *               autofilled from the hospital's doctor roster. OTP is used once, at sign-up,
 *               to confirm the doctor's mobile number.
 */

export class AuthError extends Error {
  constructor(code, message) { super(message); this.code = code; }
}

export const ROLES = {
  patient:  { id: 'patient',  label: 'Patient',  blurb: 'View and control your own records' },
  doctor:   { id: 'doctor',   label: 'Doctor',   blurb: 'Request access, add clinical notes' },
  hospital: { id: 'hospital', label: 'Hospital', blurb: 'Manage access for your doctors' }
};

export const SPECIALTIES = [
  'General Medicine', 'Cardiology', 'Endocrinology', 'Neurology', 'Orthopaedics', 'Paediatrics',
  'Gynaecology', 'Dermatology', 'Oncology', 'Psychiatry', 'Radiology', 'Pathology', 'Other'
];

/* ---------- Aadhaar checksum (Verhoeff) ---------- */
const D = [[0,1,2,3,4,5,6,7,8,9],[1,2,3,4,0,6,7,8,9,5],[2,3,4,0,1,7,8,9,5,6],[3,4,0,1,2,8,9,5,6,7],[4,0,1,2,3,9,5,6,7,8],[5,9,8,7,6,0,4,3,2,1],[6,5,9,8,7,1,0,4,3,2],[7,6,5,9,8,2,1,0,4,3],[8,7,6,5,9,3,2,1,0,4],[9,8,7,6,5,4,3,2,1,0]];
const P = [[0,1,2,3,4,5,6,7,8,9],[1,5,7,6,2,8,3,0,9,4],[5,8,0,3,7,9,6,1,4,2],[8,9,1,6,0,4,3,5,2,7],[9,4,5,3,1,2,6,8,7,0],[4,2,8,6,5,7,3,9,0,1],[2,7,9,3,8,0,6,4,1,5],[7,0,4,6,9,1,3,2,5,8]];
const INV = [0,4,3,2,1,5,6,7,8,9];

export function verhoeffValid(num) {
  let c = 0;
  [...num].reverse().forEach((ch, i) => { c = D[c][P[i % 8][+ch]]; });
  return c === 0;
}
export function verhoeffCheckDigit(payload) {
  let c = 0;
  [...payload].reverse().forEach((ch, i) => { c = D[c][P[(i + 1) % 8][+ch]]; });
  return String(INV[c]);
}

/* ---------- helpers ---------- */
export const cleanAadhaar = v => String(v || '').replace(/\D/g, '').slice(0, 12);
export const cleanMobile  = v => String(v || '').replace(/\D/g, '').replace(/^91(?=\d{10}$)/, '').slice(0, 10);
export const cleanReg     = v => String(v || '').trim().toUpperCase().replace(/\s+/g, '');
export const maskAadhaar  = a => `XXXX XXXX ${a.slice(-4)}`;
export const maskMobile   = m => `+91 ••••• •${m.slice(-4)}`;
const DOC_ID_RE = /^[A-Z0-9][A-Z0-9\-\/]{4,23}$/;
const sleep = ms => new Promise(r => setTimeout(r, ms));
const toHex = buf => [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('');
const sha256Hex = async s => toHex(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(s)));

/* Password hashing: PBKDF2-SHA256, random salt. Plaintext is never stored. */
const PBKDF2_ITER = 150000;
async function hashPassword(password, saltHex) {
  const salt = saltHex ? Uint8Array.from(saltHex.match(/../g).map(h => parseInt(h, 16))) : crypto.getRandomValues(new Uint8Array(16));
  const key = await crypto.subtle.importKey('raw', new TextEncoder().encode(password), 'PBKDF2', false, ['deriveBits']);
  const bits = await crypto.subtle.deriveBits({ name: 'PBKDF2', salt, iterations: PBKDF2_ITER, hash: 'SHA-256' }, key, 256);
  return { salt: toHex(salt), hash: toHex(bits) };
}
export function passwordProblem(pw) {
  if (!pw || pw.length < 8) return 'Use at least 8 characters.';
  if (!/[A-Za-z]/.test(pw) || !/\d/.test(pw)) return 'Include at least one letter and one number.';
  return '';
}

function memoryStore() {
  const m = new Map();
  return { getItem: k => (m.has(k) ? m.get(k) : null), setItem: (k, v) => m.set(k, String(v)), removeItem: k => m.delete(k) };
}
function pickStore(kind) {
  try { const s = window[kind]; s.setItem('__t', '1'); s.removeItem('__t'); return s; } catch { return memoryStore(); }
}

const KEYS = { accounts: 'sanjeev.accounts.v2', registry: 'sanjeev.registry.v2', session: 'sanjeev.session.v1' };

/* Hospital registry. `doctors` is each hospital's roster, used to autofill doctor sign-up. */
const DEFAULT_REGISTRY = [
  { regNo: 'MH-HOSP-2041', name: 'Apollo City Hospital', city: 'Mumbai', status: 'verified', doctors: [
      { docId: 'MMC-2011-48213', name: 'Dr. Rajesh Sharma, MD', specialty: 'Cardiology' },
      { docId: 'MMC-2014-55102', name: 'Dr. Priya Patel, MS',   specialty: 'Endocrinology' } ] },
  { regNo: 'KA-HOSP-1180', name: 'Lotus Multispecialty', city: 'Bengaluru', status: 'verified', doctors: [
      { docId: 'KMC-2016-90817', name: 'Dr. Ananya Rao, MD',    specialty: 'Neurology' } ] },
  { regNo: 'DL-CLN-0507',  name: 'Dr. Mehta Independent Clinic', city: 'New Delhi', status: 'pending',   doctors: [] },
  { regNo: 'TN-HOSP-0093', name: 'Marina General Hospital',      city: 'Chennai',   status: 'suspended', doctors: [] }
];

const OTP_TTL_MS = 5 * 60 * 1000;
const OTP_RESEND_MS = 30 * 1000;
const OTP_MAX_ATTEMPTS = 3;
const PW_MAX_ATTEMPTS = 5;
const PW_LOCK_MS = 60 * 1000;

export class AuthService extends EventTarget {
  constructor({ store, sessionStore } = {}) {
    super();
    this.store = store || pickStore('localStorage');
    this.sessionStore = sessionStore || pickStore('sessionStorage');
    this.pending = null;       // in-flight OTP challenge (memory only)
    this.pwFails = {};         // keyHash -> { n, until }
    this.demoHints = {};
    if (!this.store.getItem(KEYS.registry)) this._write(KEYS.registry, DEFAULT_REGISTRY);
  }

  _read(key, fallback) { try { return JSON.parse(this.store.getItem(key)) ?? fallback; } catch { return fallback; } }
  _write(key, val) { this.store.setItem(key, JSON.stringify(val)); }

  /* ---------- hospital registry + doctor rosters (system-admin territory) ----------
   * App-level directory data, not ledger records. Entries are never hard-deleted;
   * admins change `status` instead. */
  listHospitals() { return this._read(KEYS.registry, []); }
  getHospital(regNo) { const r = cleanReg(regNo); return this.listHospitals().find(h => h.regNo === r) || null; }
  upsertHospital({ regNo, name, city = '', status = 'verified', doctors }) {
    const reg = cleanReg(regNo);
    if (!reg || !name) throw new AuthError('BAD_INPUT', 'Registration number and name are required.');
    const old = this.getHospital(reg);
    const list = this.listHospitals().filter(h => h.regNo !== reg);
    list.push({ regNo: reg, name: name.trim(), city: city.trim(), status, doctors: doctors ?? old?.doctors ?? [] });
    this._write(KEYS.registry, list);
  }
  setHospitalStatus(regNo, status) {
    const h = this.getHospital(regNo);
    if (!h) throw new AuthError('NOT_FOUND', 'Hospital not in registry.');
    this.upsertHospital({ ...h, status });
  }
  addRosterDoctor(regNo, { docId, name, specialty }) {
    const h = this.getHospital(regNo);
    if (!h) throw new AuthError('NOT_FOUND', 'Hospital not in registry.');
    const id = cleanReg(docId);
    if (!DOC_ID_RE.test(id) || !name?.trim()) throw new AuthError('BAD_INPUT', 'Doctor ID and name are required.');
    const doctors = (h.doctors || []).filter(d => d.docId !== id).concat({ docId: id, name: name.trim(), specialty: specialty || '' });
    this.upsertHospital({ ...h, doctors });
  }
  /** Find a doctor on any hospital roster. Used for autofill. Returns null if not rostered. */
  lookupDoctor(docId) {
    const id = cleanReg(docId);
    if (!DOC_ID_RE.test(id)) return null;
    for (const h of this.listHospitals()) {
      const d = (h.doctors || []).find(x => x.docId === id);
      if (d) return { docId: id, name: d.name, specialty: d.specialty, hospital: { regNo: h.regNo, name: h.name, city: h.city, status: h.status } };
    }
    return null;
  }

  /* ---------- demo seed: ties logins to the app's DEMO_ACCOUNTS addresses ---------- */
  async seedDemo({ patient, hospital, doctors = [] }) {
    const accounts = this._read(KEYS.accounts, {});
    const aadhaar = '99990000111' + verhoeffCheckDigit('99990000111');
    const hospReg = 'MH-HOSP-2041', demoPw = 'Sanjeev@123';
    const put = async (key, acc) => { const k = await sha256Hex(key); if (!accounts[k]) accounts[k] = acc; };

    await put(`patient:${aadhaar}`, { role: 'patient', address: patient.address, name: patient.name, idMasked: maskAadhaar(aadhaar), mobile: '9876543210' });
    await put(`hospital:${hospReg}`, { role: 'hospital', address: hospital.address, name: hospital.name, idMasked: hospReg, mobile: '9820012345', hospital: { regNo: hospReg, name: 'Apollo City Hospital' } });
    for (const d of doctors) {
      const r = this.lookupDoctor(d.docId); if (!r) continue;
      await put(`doctor:${r.docId}`, { role: 'doctor', address: d.address, name: r.name, specialty: r.specialty, idMasked: r.docId, mobile: '9820054321',
        hospital: { regNo: r.hospital.regNo, name: r.hospital.name }, password: await hashPassword(demoPw) });
    }
    this._write(KEYS.accounts, accounts);
    this.demoHints = {
      patient:  { aadhaar, mobile: '9876543210' },
      hospital: { hospReg },
      doctor:   { docId: doctors[0]?.docId || 'MMC-2011-48213', password: demoPw }
    };
  }

  /* ---------- identity keys ---------- */
  _identity(role, f) {
    if (role === 'patient')  return `patient:${cleanAadhaar(f.aadhaar)}`;
    if (role === 'hospital') return `hospital:${cleanReg(f.hospReg)}`;
    return `doctor:${cleanReg(f.docId)}`;
  }

  _checkHospital(regNo, errors, key = 'hospReg') {
    const h = this.getHospital(regNo);
    if (!cleanReg(regNo)) errors[key] = 'Enter the hospital registration number.';
    else if (!h) errors[key] = 'This registration number is not in the hospital registry.';
    else if (h.status === 'pending') errors[key] = `${h.name} is awaiting approval by the system admin.`;
    else if (h.status === 'suspended') errors[key] = `${h.name} is suspended. Contact the system admin.`;
    return h;
  }

  _validate(mode, role, f) {
    const errors = {};
    const signup = mode === 'signup';
    if (role === 'patient') {
      const a = cleanAadhaar(f.aadhaar);
      if (a.length !== 12 || !/^[2-9]/.test(a) || !verhoeffValid(a)) errors.aadhaar = 'Enter a valid 12-digit Aadhaar number.';
      if (signup && !f.name?.trim()) errors.name = 'Enter your full name as on Aadhaar.';
      if (signup && !f.consent) errors.consent = 'Please accept to continue.';
    } else if (role === 'hospital') {
      this._checkHospital(f.hospReg, errors);
      if (signup && !f.name?.trim()) errors.name = 'Enter the contact person\'s name.';
    } else {
      if (!DOC_ID_RE.test(cleanReg(f.docId))) errors.docId = 'Enter your doctor registration ID (5-24 letters, digits, - or /).';
      if (!signup) {
        if (!f.password) errors.password = 'Enter your password.';
      } else if (!errors.docId) {
        const hit = this.lookupDoctor(f.docId);
        if (hit) {
          if (hit.hospital.status !== 'verified') errors.docId = `${hit.hospital.name} is ${hit.hospital.status === 'pending' ? 'awaiting approval' : 'suspended'}. Contact the system admin.`;
        } else {              // not on any roster: manual entry, hospital must be verified
          this._checkHospital(f.hospReg, errors);
          if (!f.name?.trim()) errors.name = 'Enter your full name.';
          if (!f.specialty) errors.specialty = 'Choose a specialty.';
        }
        const pwErr = passwordProblem(f.password);
        if (pwErr) errors.password = pwErr;
        else if (f.password !== f.password2) errors.password2 = 'Passwords do not match.';
      }
    }
    if (signup) {
      const m = cleanMobile(f.mobile);
      if (m.length !== 10) errors.mobile = 'Enter a 10-digit mobile number.';
      else if (!/^[6-9]/.test(m)) errors.mobile = 'Indian mobile numbers start with 6, 7, 8 or 9.';
    }
    return errors;
  }

  _fail(errors) {
    const e = new AuthError('VALIDATION', 'Check the highlighted fields.'); e.fields = errors; return e;
  }

  /* ---------- doctor password login (no OTP) ---------- */
  async loginWithPassword({ role = 'doctor', fields }) {
    if (role !== 'doctor') throw new AuthError('BAD_INPUT', 'Password login is for doctors.');
    const errors = this._validate('login', 'doctor', fields);
    if (Object.keys(errors).length) throw this._fail(errors);

    const keyHash = await sha256Hex(this._identity('doctor', fields));
    const lock = this.pwFails[keyHash];
    if (lock?.until > Date.now()) throw new AuthError('LOCKED', `Too many attempts. Try again in ${Math.ceil((lock.until - Date.now()) / 1000)}s.`);

    const acc = this._read(KEYS.accounts, {})[keyHash];
    const ok = acc?.password && (await hashPassword(fields.password, acc.password.salt)).hash === acc.password.hash;
    if (!ok) {
      const f = this.pwFails[keyHash] = { n: (lock?.n || 0) + 1, until: 0 };
      if (f.n >= PW_MAX_ATTEMPTS) { f.until = Date.now() + PW_LOCK_MS; f.n = 0; throw new AuthError('LOCKED', 'Too many attempts. Try again in 60s.'); }
      throw new AuthError('BAD_CREDENTIALS', acc ? 'Wrong ID or password.' : 'No doctor account for this ID. Sign up first, or check the ID.');
    }
    delete this.pwFails[keyHash];

    // Re-check the hospital each login: a suspended hospital locks its doctors out.
    const h = this.getHospital(acc.hospital?.regNo);
    if (!h || h.status !== 'verified') throw new AuthError('NOT_VERIFIED', 'Your hospital is not verified. Contact the system admin.');
    return this._openSession(acc, false);
  }

  /* ---------- OTP flow: patient + hospital login, and every sign-up ---------- */
  async startOtp({ mode, role, fields }) {
    if (mode === 'login' && role === 'doctor') throw new AuthError('BAD_INPUT', 'Doctors log in with a password.');
    const errors = this._validate(mode, role, fields);
    if (Object.keys(errors).length) throw this._fail(errors);

    const keyHash = await sha256Hex(this._identity(role, fields));
    const existing = this._read(KEYS.accounts, {})[keyHash];
    if (mode === 'login' && !existing) throw new AuthError('NO_ACCOUNT', 'No account found for these details. Sign up first.');
    if (mode === 'signup' && existing) throw new AuthError('EXISTS', 'An account already exists for these details. Log in instead.');

    const mobile = mode === 'login' ? existing.mobile : cleanMobile(fields.mobile);
    const { password, password2, ...safeFields } = fields;        // never keep plaintext in the pending challenge
    this.pending = { mode, role, fields: safeFields, keyHash, mobile, attempts: 0, resendAt: 0,
      passwordRecord: role === 'doctor' && mode === 'signup' ? await hashPassword(password) : null };
    await this._issueOtp();
    return { phoneMasked: maskMobile(mobile), expiresInMs: OTP_TTL_MS, resendInMs: OTP_RESEND_MS };
  }

  async _issueOtp() {
    const p = this.pending;
    // PRODUCTION SWAP: call your SMS/UIDAI OTP gateway here instead.
    const code = String(crypto.getRandomValues(new Uint32Array(1))[0] % 1_000_000).padStart(6, '0');
    p.codeHash = await sha256Hex(`${p.keyHash}:${code}`);
    p.expiresAt = Date.now() + OTP_TTL_MS;
    p.resendAt = Date.now() + OTP_RESEND_MS;
    p.attempts = 0;
    this.dispatchEvent(new CustomEvent('otp', { detail: { code, phoneMasked: maskMobile(p.mobile), role: p.role } }));
  }

  async resendOtp() {
    const p = this.pending;
    if (!p) throw new AuthError('NO_CHALLENGE', 'Start again from the sign-in form.');
    const wait = p.resendAt - Date.now();
    if (wait > 0) throw new AuthError('TOO_SOON', `Wait ${Math.ceil(wait / 1000)}s before requesting another code.`);
    await this._issueOtp();
    return { resendInMs: OTP_RESEND_MS, expiresInMs: OTP_TTL_MS };
  }

  cancelOtp() { this.pending = null; }

  async verifyOtp(code) {
    const p = this.pending;
    if (!p) throw new AuthError('NO_CHALLENGE', 'Your code session ended. Start again.');
    if (Date.now() > p.expiresAt) throw new AuthError('EXPIRED', 'This code has expired. Request a new one.');
    if (p.attempts >= OTP_MAX_ATTEMPTS) throw new AuthError('LOCKED', 'Too many wrong attempts. Request a new code.');

    const ok = (await sha256Hex(`${p.keyHash}:${String(code).trim()}`)) === p.codeHash;
    if (!ok) {
      p.attempts += 1;
      const left = OTP_MAX_ATTEMPTS - p.attempts;
      throw new AuthError('WRONG_CODE', left > 0 ? `Wrong code. ${left} attempt${left === 1 ? '' : 's'} left.` : 'Wrong code. Request a new one.');
    }

    // Dummy registry verification for hospitals and doctors
    let hospital = null;
    if (p.role !== 'patient') {
      await sleep(1300);
      const roster = p.role === 'doctor' ? this.lookupDoctor(p.fields.docId) : null;
      hospital = this.getHospital(roster ? roster.hospital.regNo : p.fields.hospReg);
      if (!hospital || hospital.status !== 'verified') throw new AuthError('NOT_VERIFIED', 'Hospital verification failed. Contact the system admin.');
    }

    const accounts = this._read(KEYS.accounts, {});
    let acc = accounts[p.keyHash];
    if (p.mode === 'signup') {
      const f = p.fields, roster = p.role === 'doctor' ? this.lookupDoctor(f.docId) : null;
      acc = {
        role: p.role,
        address: '0x' + (await sha256Hex(`addr:${p.keyHash}`)).slice(0, 40),
        name: p.role === 'hospital' ? hospital.name : roster ? roster.name : f.name.trim(),
        contactName: p.role === 'hospital' ? f.name.trim() : undefined,
        specialty: p.role === 'doctor' ? (roster ? roster.specialty : f.specialty) : undefined,
        idMasked: p.role === 'patient' ? maskAadhaar(cleanAadhaar(f.aadhaar)) : p.role === 'hospital' ? cleanReg(f.hospReg) : cleanReg(f.docId),
        mobile: cleanMobile(f.mobile),
        hospital: hospital ? { regNo: hospital.regNo, name: hospital.name } : undefined,
        password: p.passwordRecord || undefined,
        createdAt: Date.now()
      };
      accounts[p.keyHash] = acc;
      this._write(KEYS.accounts, accounts);
    }
    this.pending = null;
    return this._openSession(acc, p.mode === 'signup');
  }

  _openSession(acc, isNewAccount) {
    const session = { role: acc.role, address: acc.address, name: acc.name, specialty: acc.specialty,
      idMasked: acc.idMasked, hospital: acc.hospital, isNewAccount, issuedAt: Date.now() };
    this.sessionStore.setItem(KEYS.session, JSON.stringify(session));
    this.dispatchEvent(new CustomEvent('login', { detail: session }));
    return session;
  }

  /* ---------- session ---------- */
  getSession() { try { return JSON.parse(this.sessionStore.getItem(KEYS.session)); } catch { return null; } }
  logout() {
    this.sessionStore.removeItem(KEYS.session);
    this.dispatchEvent(new CustomEvent('logout'));
  }
}

export const auth = new AuthService();
