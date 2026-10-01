/**
 * Sanjeev WebCrypto Client Service
 * Uses standard browser window.crypto.subtle for client-side AES-GCM and SHA-256.
 * Zero plaintext medical data is sent unencrypted.
 */

export class ClientCrypto {
  /**
   * Generates a 256-bit AES symmetric key
   */
  static async generateKey() {
    return await window.crypto.subtle.generateKey(
      { name: 'AES-GCM', length: 256 },
      true,
      ['encrypt', 'decrypt']
    );
  }

  /**
   * Exports CryptoKey as hex string
   */
  static async exportKeyHex(key) {
    const raw = await window.crypto.subtle.exportKey('raw', key);
    return Array.from(new Uint8Array(raw))
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
  }

  /**
   * Imports CryptoKey from hex string
   */
  static async importKeyHex(hexStr) {
    const bytes = new Uint8Array(
      hexStr.match(/.{1,2}/g).map(byte => parseInt(byte, 16))
    );
    return await window.crypto.subtle.importKey(
      'raw',
      bytes,
      { name: 'AES-GCM', length: 256 },
      true,
      ['encrypt', 'decrypt']
    );
  }

  /**
   * Helper: converts Uint8Array to Base64 safely without blowing call stack
   */
  static bytesToBase64(bytes) {
    let binary = '';
    const len = bytes.byteLength;
    const chunkSize = 8192;
    for (let i = 0; i < len; i += chunkSize) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunkSize));
    }
    return btoa(binary);
  }

  /**
   * Helper: converts Base64 to Uint8Array safely, stripping whitespace/invalid chars
   */
  static base64ToBytes(b64) {
    if (!b64 || typeof b64 !== 'string') return new Uint8Array(0);
    const clean = b64.replace(/[^A-Za-z0-9+/=]/g, '');
    const binary = atob(clean);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) {
      bytes[i] = binary.charCodeAt(i);
    }
    return bytes;
  }

  /**
   * Encrypts plaintext string with AES-256-GCM
   */
  static async encrypt(plaintext, key) {
    const encoder = new TextEncoder();
    const data = encoder.encode(plaintext);
    const iv = window.crypto.getRandomValues(new Uint8Array(12));

    const ciphertextBuffer = await window.crypto.subtle.encrypt(
      { name: 'AES-GCM', iv },
      key,
      data
    );

    const ciphertextArray = new Uint8Array(ciphertextBuffer);
    // GCM includes the 16-byte tag at the end of the ciphertext buffer in WebCrypto
    const ciphertext = ciphertextArray.slice(0, ciphertextArray.length - 16);
    const tag = ciphertextArray.slice(ciphertextArray.length - 16);

    return {
      ciphertext_b64: ClientCrypto.bytesToBase64(ciphertext),
      iv_b64: ClientCrypto.bytesToBase64(iv),
      tag_b64: ClientCrypto.bytesToBase64(tag),
      algorithm: 'AES-256-GCM'
    };
  }

  /**
   * Decrypts AES-256-GCM payload using CryptoKey
   */
  static async decrypt(encryptedData, key) {
    const ciphertextB64 = encryptedData.ciphertext_b64 || encryptedData.ciphertext || '';
    const ivB64 = encryptedData.iv_b64 || encryptedData.iv || '';
    const tagB64 = encryptedData.tag_b64 || encryptedData.tag || '';

    const ciphertext = ClientCrypto.base64ToBytes(ciphertextB64);
    const iv = ClientCrypto.base64ToBytes(ivB64);
    const tag = ClientCrypto.base64ToBytes(tagB64);

    // Reconstruct full buffer (ciphertext + tag) for WebCrypto AES-GCM
    const combined = new Uint8Array(ciphertext.length + tag.length);
    combined.set(ciphertext, 0);
    combined.set(tag, ciphertext.length);

    const decryptedBuffer = await window.crypto.subtle.decrypt(
      { name: 'AES-GCM', iv },
      key,
      combined
    );

    const decoder = new TextDecoder();
    return decoder.decode(decryptedBuffer);
  }

  /**
   * SHA-256 hash helper
   */
  static async sha256(text) {
    const encoder = new TextEncoder();
    const data = encoder.encode(text);
    const hashBuffer = await window.crypto.subtle.digest('SHA-256', data);
    return Array.from(new Uint8Array(hashBuffer))
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');
  }

  /**
   * Generates a deterministic or random demo Ethereum-style address
   */
  static generateAddress(prefix = '0x') {
    const bytes = window.crypto.getRandomValues(new Uint8Array(20));
    return prefix + Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
  }
}
