/**
 * Sanjeev Application & Node Configuration Manager
 * Manages custom blockchain node URL and Supabase credentials with localStorage persistence.
 */

const STORAGE_KEYS = {
  NODE_URL: 'sanjeev_node_url',
  SUPABASE_URL: 'sanjeev_supabase_url',
  SUPABASE_ANON_KEY: 'sanjeev_supabase_anon_key'
};

const DEFAULT_NODE_URL = 'http://localhost:8080';
const DEFAULT_SUPABASE_URL = '';
const DEFAULT_SUPABASE_ANON_KEY = '';

export class ConfigManager {
  static getNodeUrl() {
    return localStorage.getItem(STORAGE_KEYS.NODE_URL) || DEFAULT_NODE_URL;
  }

  static setNodeUrl(url) {
    const cleaned = (url || '').trim().replace(/\/+$/, '');
    localStorage.setItem(STORAGE_KEYS.NODE_URL, cleaned || DEFAULT_NODE_URL);
    return cleaned || DEFAULT_NODE_URL;
  }

  static getSupabaseConfig() {
    return {
      url: localStorage.getItem(STORAGE_KEYS.SUPABASE_URL) || DEFAULT_SUPABASE_URL,
      anonKey: localStorage.getItem(STORAGE_KEYS.SUPABASE_ANON_KEY) || DEFAULT_SUPABASE_ANON_KEY
    };
  }

  static setSupabaseConfig(url, anonKey) {
    const cleanUrl = (url || '').trim().replace(/\/+$/, '');
    const cleanKey = (anonKey || '').trim();
    localStorage.setItem(STORAGE_KEYS.SUPABASE_URL, cleanUrl);
    localStorage.setItem(STORAGE_KEYS.SUPABASE_ANON_KEY, cleanKey);
    return { url: cleanUrl, anonKey: cleanKey };
  }

  static isSupabaseConfigured() {
    const { url, anonKey } = this.getSupabaseConfig();
    return Boolean(url && anonKey);
  }
}
