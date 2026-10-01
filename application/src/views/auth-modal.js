/**
 * Sanjeev Authentication & Wallet Link Modal
 * Handles Patient Magic Link/OTP, Staff Email+Password/Google,
 * and automatic native keypair derivation with one-time ownership signing.
 */

import { SupabaseService } from '../services/supabase.js';
import { NativeKeypairService } from '../services/native-keypair.js';
import { appState } from '../services/state.js';

export class AuthModal {
  static open(onSuccess = null) {
    this.close();
    const overlay = document.createElement('div');
    overlay.className = 'modal-overlay';
    overlay.id = 'auth-modal-overlay';
    overlay.innerHTML = `
      <div class="modal-dialog" style="max-width: 520px;">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 1.25rem;">
          <h3 style="margin: 0;">🔐 Secure Access & Keypair Link</h3>
          <button class="btn btn-secondary btn-sm" id="btn-close-auth">✕</button>
        </div>

        <!-- Role Tabs -->
        <div style="display: flex; gap: 0.5rem; margin-bottom: 1.25rem;">
          <button class="btn btn-primary btn-sm" id="auth-tab-patient" style="flex: 1;">👤 Patient (Magic Link / OTP)</button>
          <button class="btn btn-secondary btn-sm" id="auth-tab-staff" style="flex: 1;">🏥 Hospital Staff (Password / Google)</button>
        </div>

        <!-- Feedback alert -->
        <div id="auth-feedback" style="display: none; padding: 0.6rem; border-radius: var(--radius-sm); font-size: 0.85rem; margin-bottom: 1rem;"></div>

        <!-- Patient Tab Form -->
        <div id="patient-auth-form">
          <p class="text-muted" style="font-size: 0.85rem; margin-bottom: 1rem;">
            Enter your email to receive a secure one-time passcode (OTP) or magic link. Zero crypto knowledge needed.
          </p>
          <div style="margin-bottom: 0.75rem;">
            <label style="font-size: 0.75rem; color: var(--text-dim);">Patient Email Address</label>
            <input type="email" id="patient-auth-email" class="form-input" placeholder="alice@example.com">
          </div>
          <div id="otp-input-group" style="display: none; margin-bottom: 0.75rem;">
            <label style="font-size: 0.75rem; color: var(--text-dim);">6-digit OTP Code</label>
            <input type="text" id="patient-auth-otp" class="form-input" placeholder="123456" maxlength="6">
          </div>
          <div style="display: flex; gap: 0.5rem;">
            <button class="btn btn-primary" id="btn-send-otp" style="flex: 1;">Send Magic Link / OTP</button>
            <button class="btn btn-primary" id="btn-verify-otp" style="display: none; flex: 1;">Verify & Connect</button>
          </div>
        </div>

        <!-- Staff Tab Form -->
        <div id="staff-auth-form" style="display: none;">
          <p class="text-muted" style="font-size: 0.85rem; margin-bottom: 1rem;">
            Physician or hospital personnel credentials.
          </p>
          <div style="margin-bottom: 0.75rem;">
            <label style="font-size: 0.75rem; color: var(--text-dim);">Staff Work Email</label>
            <input type="email" id="staff-auth-email" class="form-input" placeholder="doctor@apollo.hospital">
          </div>
          <div style="margin-bottom: 1rem;">
            <label style="font-size: 0.75rem; color: var(--text-dim);">Password</label>
            <input type="password" id="staff-auth-password" class="form-input" placeholder="••••••••">
          </div>
          <div style="display: flex; gap: 0.5rem; margin-bottom: 0.75rem;">
            <button class="btn btn-primary" id="btn-staff-login" style="flex: 1;">Sign In</button>
            <button class="btn btn-secondary" id="btn-staff-register" style="flex: 1;">Register Staff</button>
          </div>
          <button class="btn btn-secondary" id="btn-google-login" style="width: 100%; display: flex; justify-content: center; align-items: center; gap: 0.5rem;">
            <span>🌐</span> Sign In with Google
          </button>
        </div>

        <!-- Keypair & Ownership Signature Section (Post Auth) -->
        <div id="auth-signature-panel" style="display: none; margin-top: 1rem; border-top: 1px solid var(--border-color); padding-top: 1rem;">
          <h4 style="color: var(--primary); margin-bottom: 0.5rem;">🔑 Blockchain Identity Linked</h4>
          <p style="font-size: 0.8rem; color: var(--text-muted); margin-bottom: 0.75rem;">
            Derived native wallet address: <code id="derived-address">0x...</code>
          </p>
          <button class="btn btn-primary" id="btn-sign-ownership" style="width: 100%;">
            Sign Challenge to Prove Ownership
          </button>
        </div>
      </div>
    `;

    document.body.appendChild(overlay);
    this.bindEvents(overlay, onSuccess);
  }

  static bindEvents(overlay, onSuccess) {
    const close = () => this.close();
    overlay.querySelector('#btn-close-auth').addEventListener('click', close);

    const tabPatient = overlay.querySelector('#auth-tab-patient');
    const tabStaff = overlay.querySelector('#auth-tab-staff');
    const formPatient = overlay.querySelector('#patient-auth-form');
    const formStaff = overlay.querySelector('#staff-auth-form');
    const feedback = overlay.querySelector('#auth-feedback');

    const showMsg = (msg, isErr = false) => {
      feedback.style.display = 'block';
      feedback.style.background = isErr ? 'var(--danger-bg)' : 'var(--success-bg)';
      feedback.style.border = `1px solid ${isErr ? 'var(--danger)' : 'var(--success)'}`;
      feedback.innerHTML = msg;
    };

    tabPatient.addEventListener('click', () => {
      tabPatient.className = 'btn btn-primary btn-sm';
      tabStaff.className = 'btn btn-secondary btn-sm';
      formPatient.style.display = 'block';
      formStaff.style.display = 'none';
    });

    tabStaff.addEventListener('click', () => {
      tabStaff.className = 'btn btn-primary btn-sm';
      tabPatient.className = 'btn btn-secondary btn-sm';
      formStaff.style.display = 'block';
      formPatient.style.display = 'none';
    });

    // Patient OTP Request
    overlay.querySelector('#btn-send-otp').addEventListener('click', async () => {
      const email = overlay.querySelector('#patient-auth-email').value.trim();
      if (!email) return showMsg('Please enter an email address', true);
      try {
        await SupabaseService.sendPatientOtp(email);
        showMsg(`Passcode / Magic link sent to <strong>${email}</strong>! Enter OTP below:`);
        overlay.querySelector('#otp-input-group').style.display = 'block';
        overlay.querySelector('#btn-send-otp').style.display = 'none';
        overlay.querySelector('#btn-verify-otp').style.display = 'block';
      } catch (err) {
        showMsg(err.message, true);
      }
    });

    // Patient OTP Verify
    overlay.querySelector('#btn-verify-otp').addEventListener('click', async () => {
      const email = overlay.querySelector('#patient-auth-email').value.trim();
      const token = overlay.querySelector('#patient-auth-otp').value.trim();
      try {
        const { data, error } = await SupabaseService.verifyPatientOtp(email, token);
        if (error) throw error;
        await this.handlePostAuth(data.user || { id: email, email }, 'patient', overlay, onSuccess);
      } catch (err) {
        showMsg(err.message, true);
      }
    });

    // Staff Sign In
    overlay.querySelector('#btn-staff-login').addEventListener('click', async () => {
      const email = overlay.querySelector('#staff-auth-email').value.trim();
      const pass = overlay.querySelector('#staff-auth-password').value;
      try {
        const { data, error } = await SupabaseService.signInStaff(email, pass);
        if (error) throw error;
        await this.handlePostAuth(data.user, 'hospital', overlay, onSuccess);
      } catch (err) {
        showMsg(err.message, true);
      }
    });

    // Staff Sign Up
    overlay.querySelector('#btn-staff-register').addEventListener('click', async () => {
      const email = overlay.querySelector('#staff-auth-email').value.trim();
      const pass = overlay.querySelector('#staff-auth-password').value;
      try {
        const { data, error } = await SupabaseService.signUpStaff(email, pass);
        if (error) throw error;
        showMsg('Registration complete! Check your email or sign in.');
      } catch (err) {
        showMsg(err.message, true);
      }
    });

    // Google Sign In
    overlay.querySelector('#btn-google-login').addEventListener('click', async () => {
      try {
        await SupabaseService.signInWithGoogle();
      } catch (err) {
        showMsg(err.message, true);
      }
    });
  }

  static async handlePostAuth(user, role, overlay, onSuccess) {
    const keyData = await NativeKeypairService.getOrCreateKeypair(user.id || user.email);
    const sigPanel = overlay.querySelector('#auth-signature-panel');
    const addrElem = overlay.querySelector('#derived-address');
    addrElem.innerText = keyData.address;
    sigPanel.style.display = 'block';

    overlay.querySelector('#btn-sign-ownership').addEventListener('click', async () => {
      const { signature } = await NativeKeypairService.signOwnershipChallenge(
        user.id || user.email,
        keyData.privateKey,
        keyData.address,
        user.email || 'user'
      );

      // Save to Supabase
      await SupabaseService.upsertPatient({
        userId: user.id,
        patientAddress: keyData.address,
        username: user.email ? user.email.split('@')[0] : 'patient',
        walletSignature: signature
      });

      // Update appState
      appState.currentUser = {
        address: keyData.address,
        name: user.email ? user.email.split('@')[0] : 'Sanjeev User',
        role: role,
        email: user.email,
        signature
      };
      appState.currentMode = role;
      appState.notify();

      if (onSuccess) onSuccess(appState.currentUser);
      this.close();
    });
  }

  static close() {
    const el = document.getElementById('auth-modal-overlay');
    if (el) el.remove();
  }
}
