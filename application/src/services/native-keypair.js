/**
 * Sanjeev Native Embedded Keypair & Message Signing Service
 * Generates and manages browser-native ECDSA (P-256) keypairs for users,
 * deriving addresses and signing ownership challenge messages without external wallets.
 */

import { ClientCrypto } from './crypto.js';

export class NativeKeypairService {
  /**
   * Retrieves or creates a persistent native keypair for the given user identifier
   */
  static async getOrCreateKeypair(userId) {
    const storageKey = `sanjeev_key_${userId}`;
    const saved = localStorage.getItem(storageKey);
    if (saved) {
      try {
        const data = JSON.parse(saved);
        const privateKey = await window.crypto.subtle.importKey(
          'jwk', data.privateJwk,
          { name: 'ECDSA', namedCurve: 'P-256' },
          true, ['sign']
        );
        const publicKey = await window.crypto.subtle.importKey(
          'jwk', data.publicJwk,
          { name: 'ECDSA', namedCurve: 'P-256' },
          true, ['verify']
        );
        return { address: data.address, publicKey, privateKey, rawData: data };
      } catch (err) {
        console.warn('Failed restoring keypair, generating fresh pair:', err);
      }
    }

    // Generate fresh P-256 keypair
    const keyPair = await window.crypto.subtle.generateKey(
      { name: 'ECDSA', namedCurve: 'P-256' },
      true,
      ['sign', 'verify']
    );

    const publicJwk = await window.crypto.subtle.exportKey('jwk', keyPair.publicKey);
    const privateJwk = await window.crypto.subtle.exportKey('jwk', keyPair.privateKey);

    // Derive deterministic 20-byte address from public key coordinates
    const pubCoordString = `${publicJwk.x}:${publicJwk.y}`;
    const hashHex = await ClientCrypto.sha256(pubCoordString);
    const address = '0x' + hashHex.substring(0, 40);

    const record = { address, publicJwk, privateJwk, created: Date.now() };
    localStorage.setItem(storageKey, JSON.stringify(record));

    return { address, publicKey: keyPair.publicKey, privateKey: keyPair.privateKey, rawData: record };
  }

  /**
   * Signs a one-time ownership challenge message with the user's private key
   */
  static async signOwnershipChallenge(userId, privateKey, address, emailOrUsername) {
    const timestamp = Math.floor(Date.now() / 1000);
    const challengeMessage = 
      `Sanjeev Health Portal Ownership Verification\n` +
      `User: ${emailOrUsername}\n` +
      `Wallet Address: ${address}\n` +
      `Timestamp: ${timestamp}`;

    const encoder = new TextEncoder();
    const data = encoder.encode(challengeMessage);

    const signatureBuffer = await window.crypto.subtle.sign(
      { name: 'ECDSA', hash: { name: 'SHA-256' } },
      privateKey,
      data
    );

    const signatureHex = Array.from(new Uint8Array(signatureBuffer))
      .map(b => b.toString(16).padStart(2, '0'))
      .join('');

    return {
      message: challengeMessage,
      signature: signatureHex,
      timestamp
    };
  }

  /**
   * Verifies an ownership challenge signature against the public key
   */
  static async verifySignature(message, signatureHex, publicKey) {
    try {
      const encoder = new TextEncoder();
      const data = encoder.encode(message);
      const signatureBytes = new Uint8Array(
        signatureHex.match(/.{1,2}/g).map(byte => parseInt(byte, 16))
      );

      return await window.crypto.subtle.verify(
        { name: 'ECDSA', hash: { name: 'SHA-256' } },
        publicKey,
        signatureBytes,
        data
      );
    } catch {
      return false;
    }
  }
}
