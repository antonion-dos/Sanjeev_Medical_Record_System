/**
 * Sanjeev Supabase Authentication & Clinical Data Service
 * Provides Magic Link/OTP for patients, Email/Password and Google OAuth for staff,
 * and CRUD operations for the `patients` table with symptoms, medications, and wallet address.
 */

import { ConfigManager } from '../config/node-config.js';

let supabaseClient = null;

export class SupabaseService {
  static async getClient() {
    if (supabaseClient) return supabaseClient;
    const { url, anonKey } = ConfigManager.getSupabaseConfig();
    if (!url || !anonKey) return null;

    try {
      const { createClient } = await import('https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.39.8/+esm');
      supabaseClient = createClient(url, anonKey, {
        auth: { persistSession: true, autoRefreshToken: true }
      });
      return supabaseClient;
    } catch (err) {
      console.warn('Failed loading Supabase JS client from CDN:', err);
      return null;
    }
  }

  // --- Authentication Methods ---

  static async sendPatientOtp(email) {
    const client = await this.getClient();
    if (!client) throw new Error('Supabase is not configured. Please enter credentials in Settings (⚙️).');
    return await client.auth.signInWithOtp({
      email,
      options: { emailRedirectTo: window.location.origin }
    });
  }

  static async verifyPatientOtp(email, token) {
    const client = await this.getClient();
    if (!client) throw new Error('Supabase is not configured.');
    return await client.auth.verifyOtp({ email, token, type: 'email' });
  }

  static async signInStaff(email, password) {
    const client = await this.getClient();
    if (!client) throw new Error('Supabase is not configured.');
    return await client.auth.signInWithPassword({ email, password });
  }

  static async signUpStaff(email, password, fullName = '') {
    const client = await this.getClient();
    if (!client) throw new Error('Supabase is not configured.');
    return await client.auth.signUp({
      email,
      password,
      options: { data: { full_name: fullName, role: 'hospital_staff' } }
    });
  }

  static async signInWithGoogle() {
    const client = await this.getClient();
    if (!client) throw new Error('Supabase is not configured.');
    return await client.auth.signInWithOAuth({
      provider: 'google',
      options: { redirectTo: window.location.origin }
    });
  }

  static async signOut() {
    const client = await this.getClient();
    if (client) await client.auth.signOut();
    localStorage.removeItem('sanjeev_active_user');
  }

  static async getCurrentUser() {
    const client = await this.getClient();
    if (!client) return null;
    const { data: { user } } = await client.auth.getUser();
    return user;
  }

  // --- Patients Table CRUD Methods ---

  static async upsertPatient({ userId, patientAddress, username, doctor = '', symptoms = [], medications = [], patientData = {}, walletSignature = '' }) {
    const client = await this.getClient();
    if (!client) return { error: 'Supabase offline/not configured' };

    return await client.from('patients').upsert({
      user_id: userId,
      patient_address: patientAddress,
      username,
      doctor,
      symptoms,
      medications,
      patient_data: patientData,
      wallet_signature: walletSignature,
      updated_at: new Date().toISOString()
    }, { onConflict: 'patient_address' });
  }

  static async getPatient(patientAddress) {
    const client = await this.getClient();
    if (!client) return null;
    const { data, error } = await client.from('patients')
      .select('*')
      .eq('patient_address', patientAddress)
      .maybeSingle();
    return error ? null : data;
  }

  static async listPatientsForDoctor(doctorNameOrAddress) {
    const client = await this.getClient();
    if (!client) return [];
    const { data, error } = await client.from('patients')
      .select('*')
      .eq('doctor', doctorNameOrAddress);
    return error ? [] : data;
  }
}
