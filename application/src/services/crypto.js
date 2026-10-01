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
      ciphertext_b64: btoa(String.fromCharCode(...ciphertext)),
      iv_b64: btoa(String.fromCharCode(...iv)),
      tag_b64: btoa(String.fromCharCode(...tag)),
      algorithm: 'AES-256-GCM'
    };
  }

  /**
   * Decrypts AES-256-GCM payload using CryptoKey
   */
  static async decrypt(encryptedData, key) {
    const ciphertext = Uint8Array.from(atob(encryptedData.ciphertext_b64), c => c.charCodeAt(0));
    const iv = Uint8Array.from(atob(encryptedData.iv_b64), c => c.charCodeAt(0));
    const tag = Uint8Array.from(atob(encryptedData.tag_b64), c => c.charCodeAt(0));

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
   * Converts a Base64 string to a lowercase hexadecimal string
   */
  static base64ToHex(b64) {
    if (!b64) return '';
    try {
      const raw = atob(b64);
      let hex = '';
      for (let i = 0; i < raw.length; i++) {
        hex += raw.charCodeAt(i).toString(16).padStart(2, '0');
      }
      return hex;
    } catch {
      return '';
    }
  }

  /**
   * Converts a hexadecimal string to a Base64 string
   */
  static hexToBase64(hex) {
    if (!hex) return '';
    try {
      const cleanHex = hex.startsWith('0x') ? hex.slice(2) : hex;
      const bytes = new Uint8Array(cleanHex.match(/.{1,2}/g).map(byte => parseInt(byte, 16)));
      let binary = '';
      for (let i = 0; i < bytes.length; i++) {
        binary += String.fromCharCode(bytes[i]);
      }
      return btoa(binary);
    } catch {
      return '';
    }
  }
}

