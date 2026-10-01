/**
 * AuthModal - log in / sign up popup with role choice, OTP, and doctor password login.
 *
 *   const modal = new AuthModal({
 *     auth,
 *     dismissible: false,
 *     handlers: { patient: fn, doctor: fn, hospital: fn }   // one separate call per role
 *   });
 *   modal.open();                                           // login, patient
 *   modal.open({ mode: 'signup', role: 'doctor' });
 */
import { ROLES, SPECIALTIES, cleanAadhaar, cleanMobile, cleanReg } from '../services/auth.js';

const esc = s => String(s ?? '').replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
const LAST_DOC_KEY = 'sanjeev.lastDoctorId';
const store = { get: k => { try { return localStorage.getItem(k); } catch { return null; } }, set: (k, v) => { try { localStorage.setItem(k, v); } catch {} }, del: k => { try { localStorage.removeItem(k); } catch {} } };

const ICONS = {
  patient:  '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="8" r="3.6"/><path d="M5 20c.6-3.6 3.3-5.6 7-5.6s6.4 2 7 5.6"/></svg>',
  doctor:   '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M7 3v6a4 4 0 0 0 8 0V3"/><path d="M11 13v2.5a4.5 4.5 0 0 0 9 0V13"/><circle cx="20" cy="11" r="2"/></svg>',
  hospital: '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 21V7l8-4 8 4v14"/><path d="M2 21h20"/><path d="M12 9v6M9 12h6"/></svg>',
  close:    '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6L6 18"/></svg>',
  check:    '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M5 12.5l4.2 4.2L19 7"/></svg>',
  shield:   '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3l7 3v5c0 4.6-2.9 8.2-7 10-4.1-1.8-7-5.4-7-10V6z"/><path d="M9 12l2.2 2.2L15.5 10"/></svg>',
  eye:      '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M2 12s3.6-6.5 10-6.5S22 12 22 12s-3.6 6.5-10 6.5S2 12 2 12z"/><circle cx="12" cy="12" r="3"/></svg>'
};

export class AuthModal {
  constructor({ auth, handlers = {}, onAuthenticated = () => {}, dismissible = true } = {}) {
    this.auth = auth;
    this.handlers = handlers;
    this.onAuthenticated = onAuthenticated;   // fallback if a role has no handler
    this.dismissible = dismissible;
    this.state = this._fresh('login', 'patient');
    this.timers = [];
    this.root = null;
    this._onOtp = e => this._showSms(e.detail);
    this._onKey = e => this._keydown(e);
  }

  _fresh(mode, role) {
    return { mode, role, step: 'details', fields: {}, errors: {}, busy: false, info: null, remember: true };
  }

  /* ---------------- lifecycle ---------------- */
  open({ mode = 'login', role = 'patient' } = {}) {
    if (this.root) return;
    this.returnFocus = document.activeElement;
    this.state = this._fresh(mode, role);
    this._prefillRemembered();

    this.root = document.createElement('div');
    this.root.className = 'sj-auth';
    this.root.innerHTML = `
      <div class="sj-auth__scrim" data-action="scrim"></div>
      <section class="sj-auth__panel" role="dialog" aria-modal="true" aria-labelledby="sj-auth-title" tabindex="-1"></section>`;
    document.body.appendChild(this.root);
    document.body.classList.add('sj-auth-lock');
    this.panel = this.root.querySelector('.sj-auth__panel');

    this.root.addEventListener('click', e => this._click(e));
    this.root.addEventListener('input', e => this._input(e));
    this.root.addEventListener('submit', e => { e.preventDefault(); this._submit(); });
    this.root.addEventListener('keydown', e => this._otpKeys(e));
    this.root.addEventListener('paste', e => this._otpPaste(e));
    document.addEventListener('keydown', this._onKey);
    this.auth.addEventListener('otp', this._onOtp);

    this.render();
    requestAnimationFrame(() => this.root?.classList.add('is-open'));
  }

  close() {
    if (!this.root) return;
    this.auth.cancelOtp();
    this.timers.forEach(clearInterval); this.timers = [];
    document.removeEventListener('keydown', this._onKey);
    this.auth.removeEventListener('otp', this._onOtp);
    this._hideSms();
    const root = this.root; this.root = null;
    root.classList.remove('is-open');
    document.body.classList.remove('sj-auth-lock');
    setTimeout(() => root.remove(), 200);
    this.returnFocus?.focus?.();
  }

  _prefillRemembered() {
    const id = store.get(LAST_DOC_KEY);
    if (id) this.rememberedDoc = id; else this.rememberedDoc = null;
  }
  _applyRememberedIfNeeded() {
    const s = this.state;
    if (s.role === 'doctor' && s.mode === 'login' && !s.fields.docId && this.rememberedDoc) s.fields.docId = this.rememberedDoc;
  }

  /* ---------------- rendering ---------------- */
  render(focusSel) {
    this._applyRememberedIfNeeded();
    const { step } = this.state;
    this.panel.dataset.role = this.state.role;
    this.panel.innerHTML = `
      ${this.dismissible && step !== 'done' ? `<button class="sj-auth__close" data-action="close" aria-label="Close">${ICONS.close}</button>` : ''}
      ${step === 'details' ? this._detailsView() : step === 'otp' ? this._otpView() : step === 'verifying' ? this._verifyingView() : this._doneView()}`;
    const target = (focusSel && this.panel.querySelector(focusSel)) || this.panel.querySelector('[autofocus]') || this.panel;
    target.focus({ preventScroll: true });
    if (step === 'otp') this._startCountdown();
    if (step === 'details') { this._updateChip(); this._updateDoctorLookup(); }
  }

  _isDoctorLogin() { return this.state.role === 'doctor' && this.state.mode === 'login'; }

  _detailsView() {
    const { mode, role, errors, busy, info } = this.state;
    const roleBtns = Object.values(ROLES).map(r => `
      <button type="button" role="radio" aria-checked="${r.id === role}" class="sj-role${r.id === role ? ' is-on' : ''}" data-action="role" data-role="${r.id}">
        <span class="sj-role__icon">${ICONS[r.id]}</span>
        <span class="sj-role__label">${r.label}</span>
      </button>`).join('');
    const cta = this._isDoctorLogin() ? (busy ? 'Logging in…' : 'Log in') : (busy ? 'Sending code…' : 'Send OTP');

    return `
      <header class="sj-auth__head">
        <h2 id="sj-auth-title">${mode === 'login' ? 'Welcome back' : 'Create your account'}</h2>
        <p class="sj-auth__sub">${ROLES[role].blurb}.</p>
      </header>

      <div class="sj-seg" role="tablist" aria-label="Log in or sign up">
        <button type="button" role="tab" aria-selected="${mode === 'login'}" class="${mode === 'login' ? 'is-on' : ''}" data-action="mode" data-mode="login">Log in</button>
        <button type="button" role="tab" aria-selected="${mode === 'signup'}" class="${mode === 'signup' ? 'is-on' : ''}" data-action="mode" data-mode="signup">Sign up</button>
      </div>

      <form class="sj-form" novalidate autocomplete="off">
        <div class="sj-field">
          <span class="sj-label" id="sj-role-l">I am a</span>
          <div class="sj-roles" role="radiogroup" aria-labelledby="sj-role-l">${roleBtns}</div>
        </div>
        ${this._fields()}
        ${info ? `<p class="sj-note" role="status">${esc(info)}</p>` : ''}
        ${errors._form ? `<p class="sj-error sj-error--form" role="alert">${esc(errors._form)}</p>` : ''}
        <button class="sj-btn" type="submit" ${busy ? 'disabled' : ''}>${cta}</button>
      </form>
      ${this._demoChips()}
      <p class="sj-foot">${ICONS.shield}<span>Your records stay encrypted. Signing in only proves who you are.</span></p>`;
  }

  _field(name, label, { type = 'text', placeholder = '', inputmode = '', hint = '', autofocus = false, extra = '', prefix = '', toggle = false } = {}) {
    const f = this.state.fields, err = this.state.errors[name];
    const raw = f[name] || '';
    const shown = name === 'aadhaar' ? raw.replace(/(\d{4})(?=\d)/g, '$1 ') : name === 'mobile' ? raw.replace(/^(\d{5})(?=\d)/, '$1 ') : raw;
    return `
      <div class="sj-field${err ? ' has-error' : ''}">
        <label class="sj-label" for="sj-${name}">${label}</label>
        <div class="sj-input">${prefix ? `<span class="sj-input__prefix">${prefix}</span>` : ''}
          <input id="sj-${name}" name="${name}" type="${type}" value="${esc(shown)}" placeholder="${esc(placeholder)}"
            ${inputmode ? `inputmode="${inputmode}"` : ''} ${autofocus ? 'autofocus' : ''} autocomplete="${type === 'password' ? 'off' : 'off'}" spellcheck="false"
            aria-invalid="${!!err}" ${err ? `aria-describedby="sj-${name}-e"` : ''}>
          ${toggle ? `<button type="button" class="sj-input__btn" data-action="toggle-pw" aria-label="Show password" aria-pressed="false">${ICONS.eye}</button>` : ''}
        </div>
        ${extra}
        ${hint && !err ? `<p class="sj-hint">${hint}</p>` : ''}
        ${err ? `<p class="sj-error" id="sj-${name}-e" role="alert">${esc(err)}</p>` : ''}
      </div>`;
  }

  _fields() {
    const { mode, role, fields, errors } = this.state, signup = mode === 'signup';
    const hospChip = '<p class="sj-chip" data-chip aria-live="polite"></p>';
    const mobile = signup ? this._field('mobile', 'Mobile number', { type: 'tel', inputmode: 'numeric', placeholder: '98765 43210', prefix: '+91',
      hint: role === 'patient' ? 'Use the number linked to your Aadhaar. We send your OTP here.' : 'We send a one-time code here to confirm it.' }) : '';

    if (role === 'patient') return `
      ${this._field('aadhaar', 'Aadhaar number', { inputmode: 'numeric', placeholder: '0000 0000 0000', autofocus: true,
        hint: mode === 'login' ? 'We send an OTP to the mobile number linked to it.' : '' })}
      ${signup ? this._field('name', 'Full name', { placeholder: 'As printed on Aadhaar' }) : ''}
      ${mobile}
      ${signup ? `
      <label class="sj-check${errors.consent ? ' has-error' : ''}">
        <input type="checkbox" name="consent" ${fields.consent ? 'checked' : ''}>
        <span>I agree to Sanjeev storing my records on the blockchain. Only I decide who can read them.</span>
      </label>${errors.consent ? `<p class="sj-error" role="alert">${esc(errors.consent)}</p>` : ''}` : ''}`;

    if (role === 'hospital') return `
      ${this._field('hospReg', 'Hospital registration number', { placeholder: 'e.g. MH-HOSP-2041', autofocus: true, extra: hospChip })}
      ${signup ? this._field('name', 'Contact person', { placeholder: 'Authorised signatory' }) : ''}
      ${mobile}`;

    /* doctor */
    const docChip = '<p class="sj-chip" data-doc-chip aria-live="polite"></p>';
    const idField = this._field('docId', 'Doctor registration ID', { placeholder: 'e.g. MMC-2011-48213', autofocus: !this.state.fields.docId, extra: docChip });
    if (!signup) return `
      ${idField}
      ${this._field('password', 'Password', { type: 'password', placeholder: 'Your password', toggle: true, autofocus: !!this.state.fields.docId })}
      <label class="sj-check"><input type="checkbox" name="remember" ${this.state.remember ? 'checked' : ''}><span>Remember my ID on this device</span></label>`;

    const spec = `
      <div class="sj-field${errors.specialty ? ' has-error' : ''}">
        <label class="sj-label" for="sj-specialty">Specialty</label>
        <div class="sj-input"><select id="sj-specialty" name="specialty" aria-invalid="${!!errors.specialty}">
          <option value="">Choose a specialty</option>
          ${SPECIALTIES.map(s => `<option ${fields.specialty === s ? 'selected' : ''}>${s}</option>`).join('')}
        </select></div>
        ${errors.specialty ? `<p class="sj-error" role="alert">${esc(errors.specialty)}</p>` : ''}
      </div>`;
    return `
      ${idField}
      <div class="sj-auto" data-auto hidden></div>
      <div class="sj-manual" data-manual>
        ${this._field('hospReg', 'Hospital registration number', { placeholder: 'e.g. MH-HOSP-2041', extra: hospChip })}
        ${this._field('name', 'Full name', { placeholder: 'Dr. Full Name' })}
        ${spec}
      </div>
      ${mobile}
      ${this._field('password', 'Create password', { type: 'password', placeholder: 'At least 8 characters', toggle: true, hint: 'Use a letter and a number.' })}
      ${this._field('password2', 'Confirm password', { type: 'password', placeholder: 'Repeat password' })}`;
  }

  _demoChips() {
    const h = this.auth.demoHints?.[this.state.role];
    if (!h || this.state.mode !== 'login') return '';
    return `<div class="sj-demo"><span>Demo account</span>
      <button type="button" data-action="demo">Fill ${ROLES[this.state.role].label.toLowerCase()} details</button></div>`;
  }

  _otpView() {
    const { info } = this.state;
    return `
      <header class="sj-auth__head">
        <h2 id="sj-auth-title">Enter your code</h2>
        <p class="sj-auth__sub">We sent a 6-digit code to <strong>${esc(info.phoneMasked)}</strong>.</p>
      </header>
      <form class="sj-form" novalidate>
        <div class="sj-otp" role="group" aria-label="6-digit one-time password">
          ${[0, 1, 2, 3, 4, 5].map(i => `<input class="sj-otp__box" inputmode="numeric" maxlength="1" autocomplete="${i === 0 ? 'one-time-code' : 'off'}" aria-label="Digit ${i + 1}" data-i="${i}" ${i === 0 ? 'autofocus' : ''}>`).join('')}
        </div>
        <p class="sj-error sj-error--form" role="alert" data-otp-error>${esc(this.state.errors._form || '')}</p>
        <button class="sj-btn" type="submit" ${this.state.busy ? 'disabled' : ''}>${this.state.mode === 'login' ? 'Log in' : 'Create account'}</button>
      </form>
      <div class="sj-otp-meta">
        <button type="button" class="sj-link" data-action="back">Change details</button>
        <span><span data-expiry></span> <button type="button" class="sj-link" data-action="resend" disabled data-resend>Resend</button></span>
      </div>`;
  }

  _verifyingView() {
    const msg = this.state.pwLogin ? 'Checking your password…' : this.state.role === 'patient' ? 'Confirming your code…' : 'Checking the hospital registry…';
    return `
      <div class="sj-status" role="status" aria-live="polite">
        <div class="sj-spinner" aria-hidden="true"></div>
        <h2 id="sj-auth-title">Verifying</h2>
        <p class="sj-auth__sub">${msg}</p>
      </div>`;
  }

  _doneView() {
    const s = this.state.session;
    const line2 = [s.specialty, s.hospital?.name].filter(Boolean).join(', ');
    return `
      <div class="sj-status" role="status" aria-live="polite">
        <div class="sj-tick" aria-hidden="true">${ICONS.check}</div>
        <h2 id="sj-auth-title">${s.isNewAccount ? 'Account created' : 'You are signed in'}</h2>
        <p class="sj-auth__sub">${esc(s.name)}${line2 ? `<br>${esc(line2)}` : ''}<br><span class="sj-mono">${esc(s.idMasked)}</span></p>
      </div>`;
  }

  /* ---------------- events ---------------- */
  _click(e) {
    const el = e.target.closest('[data-action]'); if (!el) return;
    const a = el.dataset.action, s = this.state;
    if (a === 'scrim' || a === 'close') { if (this.dismissible) this.close(); return; }
    if (a === 'role') { s.role = el.dataset.role; s.errors = {}; s.fields = {}; this.render(`[data-role="${s.role}"]`); }
    if (a === 'mode') { s.mode = el.dataset.mode; s.errors = {}; s.fields = {}; this.render(`[data-mode="${s.mode}"]`); }
    if (a === 'demo') { s.fields = { ...this.auth.demoHints[s.role] }; s.errors = {}; this.render(); }
    if (a === 'back') { this.auth.cancelOtp(); this._hideSms(); this.timers.forEach(clearInterval); s.step = 'details'; s.errors = {}; s.info = null; this.render(); }
    if (a === 'resend') this._resend();
    if (a === 'toggle-pw') {
      const inp = el.closest('.sj-input').querySelector('input'), show = inp.type === 'password';
      inp.type = show ? 'text' : 'password';
      el.setAttribute('aria-pressed', String(show)); el.setAttribute('aria-label', show ? 'Hide password' : 'Show password');
    }
    if (a === 'sms-dismiss') this._hideSms();
  }

  _input(e) {
    const t = e.target, s = this.state;
    if (t.classList.contains('sj-otp__box')) return this._otpInput(t);
    if (!t.name) return;
    if (t.type === 'checkbox') { s.fields[t.name] = t.checked; if (t.name === 'remember') s.remember = t.checked; return; }
    let v = t.value;
    if (t.name === 'aadhaar') { v = cleanAadhaar(v); t.value = v.replace(/(\d{4})(?=\d)/g, '$1 '); }
    else if (t.name === 'mobile') { v = cleanMobile(v); t.value = v.replace(/^(\d{5})(?=\d)/, '$1 '); }
    else if (t.name === 'hospReg' || t.name === 'docId') { v = cleanReg(v); t.value = v; }
    s.fields[t.name] = v;
    if (t.name === 'hospReg') this._updateChip();
    if (t.name === 'docId') this._updateDoctorLookup();
    if (s.errors[t.name]) {
      delete s.errors[t.name];
      const f = t.closest('.sj-field'); f?.classList.remove('has-error'); t.setAttribute('aria-invalid', 'false');
      f?.querySelector('.sj-error')?.remove();
    }
  }

  _updateChip() {
    const chip = this.panel?.querySelector('[data-chip]'); if (!chip) return;
    const reg = this.state.fields.hospReg;
    if (!reg) { chip.textContent = ''; chip.className = 'sj-chip'; return; }
    const h = this.auth.getHospital(reg);
    if (!h) { chip.textContent = 'Not found in registry'; chip.className = 'sj-chip is-bad'; return; }
    const map = { verified: ['is-ok', 'Verified'], pending: ['is-warn', 'Awaiting approval'], suspended: ['is-bad', 'Suspended'] }[h.status];
    chip.className = `sj-chip ${map[0]}`;
    chip.textContent = `${h.name}, ${h.city}: ${map[1]}`;
  }

  /** Doctor autofill: look the ID up on hospital rosters, fill name / specialty / hospital. */
  _updateDoctorLookup() {
    const s = this.state; if (s.role !== 'doctor' || !this.panel) return;
    const chip = this.panel.querySelector('[data-doc-chip]'); if (!chip) return;
    const id = s.fields.docId || '';
    const hit = id.length >= 5 ? this.auth.lookupDoctor(id) : null;
    const auto = this.panel.querySelector('[data-auto]'), manual = this.panel.querySelector('[data-manual]');
    const signup = s.mode === 'signup';

    if (hit) {
      const ok = hit.hospital.status === 'verified';
      chip.className = `sj-chip ${ok ? 'is-ok' : 'is-warn'}`;
      chip.textContent = signup ? 'Found on hospital roster' : `${hit.name}, ${hit.hospital.name}`;
      if (signup && auto && manual) {
        s.fields.name = hit.name; s.fields.specialty = hit.specialty; s.fields.hospReg = hit.hospital.regNo; s.rosterFilled = true;
        auto.innerHTML = `
          <div class="sj-auto__row"><span>Name</span><strong>${esc(hit.name)}</strong></div>
          <div class="sj-auto__row"><span>Specialty</span><strong>${esc(hit.specialty || 'Not set')}</strong></div>
          <div class="sj-auto__row"><span>Hospital</span><strong>${esc(hit.hospital.name)}, ${esc(hit.hospital.city)}</strong></div>
          <p class="sj-hint">Filled from your hospital's records. Wrong details? Ask your hospital admin to update the roster.</p>`;
        auto.hidden = false; manual.hidden = true;
      }
    } else {
      if (signup && manual && auto) {
        if (s.rosterFilled) {                      // roster match was lost: clear what we autofilled
          ['hospReg', 'name', 'specialty'].forEach(k => { delete s.fields[k]; const i = manual.querySelector(`[name="${k}"]`); if (i) i.value = ''; });
          s.rosterFilled = false;
        }
        auto.hidden = true; manual.hidden = false;
        chip.className = id.length >= 5 ? 'sj-chip is-warn' : 'sj-chip';
        chip.textContent = id.length >= 5 ? 'Not on a hospital roster. Enter your details below.' : '';
        this._updateChip();
      } else { chip.className = 'sj-chip'; chip.textContent = ''; }
    }
  }

  async _submit() {
    const s = this.state;
    if (s.busy) return;
    if (s.step === 'otp') return this._verify();
    if (s.step !== 'details') return;
    if (this._isDoctorLogin()) return this._passwordLogin();
    s.busy = true; s.errors = {}; this._setBusy('Sending code…');
    try {
      s.info = await this.auth.startOtp({ mode: s.mode, role: s.role, fields: s.fields });
      s.step = 'otp'; s.busy = false; this.render();
    } catch (err) { this._showError(err); }
  }

  async _passwordLogin() {
    const s = this.state;
    s.busy = true; s.errors = {}; this._setBusy('Logging in…');
    try {
      s.pwLogin = true; s.step = 'verifying'; this.render();
      const session = await this.auth.loginWithPassword({ role: 'doctor', fields: s.fields });
      if (s.remember) store.set(LAST_DOC_KEY, session.idMasked); else store.del(LAST_DOC_KEY);
      this._complete(session);
    } catch (err) { s.pwLogin = false; s.step = 'details'; this._showError(err); }
  }

  _showError(err) {
    const s = this.state;
    s.busy = false; s.errors = err.fields || { _form: err.message };
    this.render(err.fields ? '.has-error input, .has-error select' : null);
    this.panel.classList.remove('shake'); void this.panel.offsetWidth; this.panel.classList.add('shake');
  }

  _setBusy(label) {
    const b = this.panel.querySelector('.sj-btn'); if (!b) return;
    b.disabled = true; b.textContent = label;
  }

  /** One separate call per role. Falls back to onAuthenticated. */
  _complete(session) {
    const s = this.state;
    s.session = session; s.step = 'done'; s.busy = false; this._hideSms(); this.render();
    setTimeout(() => {
      const fn = this.handlers[session.role] || this.onAuthenticated;
      this.close();
      fn(session);
    }, 1100);
  }

  /* ---------------- OTP boxes ---------------- */
  get boxes() { return [...this.panel.querySelectorAll('.sj-otp__box')]; }
  _otpInput(t) {
    t.value = t.value.replace(/\D/g, '').slice(-1);
    const i = +t.dataset.i;
    if (t.value && i < 5) this.boxes[i + 1].focus();
    this._setOtpError('');
    if (this.boxes.every(b => b.value)) this._verify();
  }
  _otpKeys(e) {
    const t = e.target; if (!t.classList?.contains('sj-otp__box')) return;
    const i = +t.dataset.i;
    if (e.key === 'Backspace' && !t.value && i > 0) { this.boxes[i - 1].value = ''; this.boxes[i - 1].focus(); }
    if (e.key === 'ArrowLeft' && i > 0) this.boxes[i - 1].focus();
    if (e.key === 'ArrowRight' && i < 5) this.boxes[i + 1].focus();
  }
  _otpPaste(e) {
    if (!e.target.classList?.contains('sj-otp__box')) return;
    const digits = (e.clipboardData?.getData('text') || '').replace(/\D/g, '').slice(0, 6);
    if (!digits) return;
    e.preventDefault();
    this.boxes.forEach((b, i) => { b.value = digits[i] || ''; });
    this.boxes[Math.min(digits.length, 5)].focus();
    if (digits.length === 6) this._verify();
  }
  _setOtpError(msg) { const el = this.panel.querySelector('[data-otp-error]'); if (el) el.textContent = msg; }

  async _verify() {
    const s = this.state; if (s.busy) return;
    const code = this.boxes.map(b => b.value).join('');
    if (code.length < 6) return this._setOtpError('Enter all 6 digits.');
    s.busy = true;
    s.step = 'verifying'; this.timers.forEach(clearInterval); this.render();
    try {
      const session = await this.auth.verifyOtp(code);
      this._complete(session);
    } catch (err) {
      s.busy = false; s.step = 'otp'; s.errors = { _form: err.message };
      this.render();
      this.boxes.forEach(b => b.classList.add('is-bad'));
      this.boxes[0].focus();
      this._setOtpError(err.message);
      if (['EXPIRED', 'LOCKED', 'NO_CHALLENGE'].includes(err.code)) this.panel.querySelector('[data-resend]')?.removeAttribute('disabled');
    }
  }

  async _resend() {
    try {
      const r = await this.auth.resendOtp();
      this.state.errors = {}; this.render('.sj-otp__box');
      this._setOtpError('');
      this._resendAt = Date.now() + r.resendInMs; this._expiresAt = Date.now() + r.expiresInMs;
      this._startCountdown(true);
    } catch (err) { this._setOtpError(err.message); }
  }

  _startCountdown(reuse = false) {
    this.timers.forEach(clearInterval); this.timers = [];
    if (!reuse) { this._resendAt = Date.now() + this.state.info.resendInMs; this._expiresAt = Date.now() + this.state.info.expiresInMs; }
    const tick = () => {
      const exp = this.panel?.querySelector('[data-expiry]'), rs = this.panel?.querySelector('[data-resend]');
      if (!exp || !rs) return;
      const left = Math.max(0, Math.ceil((this._resendAt - Date.now()) / 1000));
      const ttl = Math.max(0, Math.ceil((this._expiresAt - Date.now()) / 1000));
      exp.textContent = ttl > 0 ? `Expires in ${Math.floor(ttl / 60)}:${String(ttl % 60).padStart(2, '0')}.` : 'Code expired.';
      rs.disabled = left > 0 && ttl > 0;
      rs.textContent = left > 0 && ttl > 0 ? `Resend in ${left}s` : 'Resend';
    };
    tick(); this.timers.push(setInterval(tick, 1000));
  }

  /* ---------------- simulated SMS broadcast (demo) ---------------- */
  _showSms({ code, phoneMasked }) {
    this._hideSms();
    const el = document.createElement('aside');
    el.className = 'sj-sms'; el.setAttribute('role', 'status');
    el.innerHTML = `
      <div class="sj-sms__app"><span>Messages</span><span>now</span></div>
      <p class="sj-sms__body"><strong>${code}</strong> is your Sanjeev verification code. It expires in 5 minutes. Do not share it.</p>
      <p class="sj-sms__demo">Demo broadcast to ${esc(phoneMasked)}</p>
      <button type="button" class="sj-sms__x" aria-label="Dismiss message">${ICONS.close}</button>`;
    el.querySelector('.sj-sms__x').addEventListener('click', () => this._hideSms());
    document.body.appendChild(el);
    requestAnimationFrame(() => el.classList.add('is-in'));
    this.sms = el;
  }
  _hideSms() { this.sms?.remove(); this.sms = null; }

  /* ---------------- a11y ---------------- */
  _keydown(e) {
    if (!this.root) return;
    if (e.key === 'Escape' && this.dismissible && this.state.step !== 'verifying') return this.close();
    if (e.key !== 'Tab') return;
    const f = [...this.panel.querySelectorAll('button:not([disabled]), input:not([hidden]), select, [tabindex="0"]')].filter(n => n.offsetParent !== null);
    if (!f.length) return;
    const first = f[0], last = f[f.length - 1];
    if (e.shiftKey && (document.activeElement === first || document.activeElement === this.panel)) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  }
}
