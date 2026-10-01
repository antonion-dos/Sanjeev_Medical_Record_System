# FixIt: Blockchain Node & Web Application Integration Guide

This document identifies the root causes preventing the Web Application (`application/`) from communicating with the C++ Blockchain Node (`blockchain/`), and provides step-by-step instructions for the **Application Agent** to resolve them.

---

## 🔍 Verification Findings Summary

| Subsystem | Health / Status | Details |
| :--- | :---: | :--- |
| **Blockchain Node (C++)** | 🟢 **Healthy & Online** | Daemon running at `http://localhost:8080`. Responds to `GET /api/v1/chain/status` and `GET /api/v1/chain/blocks`. All 6/6 test suites pass with 100%. |
| **Node Connection Indicator** | 🟢 **Connected** | App header correctly shows `Node: Online (8080)`. |
| **Hospital Portal UI** | 🟢 **Rendering** | Shows doctors, clinical tabs, and delegation panels. |
| **Patient Portal & Explorer** | 🔴 **Broken (JS Crash)** | Throws uncaught exceptions on load, preventing views from rendering. |
| **API Endpoints & Schemas** | 🔴 **Mismatched** | App calls `/api/records` and `/api/keys/temporal`, but Node exposes `/api/v1/blob/store` and `/api/v1/token/grant`. |

---

## 🐛 Bug #1: Uncaught `TypeError` in `initLocalLedger` (CRITICAL)

### Root Cause
In [application/src/services/api.js](application/src/services/api.js#L120):
```javascript
// Line 120 of api.js:
recipient: DEMO_ACCOUNTS.doctor_rajesh.address, // <-- BUG: DEMO_ACCOUNTS.doctor_rajesh is UNDEFINED!
```
In [application/src/services/state.js](application/src/services/state.js#L18-L34), `DEMO_ACCOUNTS` defines doctors inside an array: `DEMO_ACCOUNTS.hospital.doctors[0]`. There is no `DEMO_ACCOUNTS.doctor_rajesh`.

### Impact
When `initLocalLedger()` runs during `BlockchainApi` construction, evaluating `DEMO_ACCOUNTS.doctor_rajesh.address` throws:
```text
TypeError: Cannot read properties of undefined (reading 'address')
```
Because of this uncaught exception:
1. `this.localLedger` is **never initialized** (`undefined`).
2. Subsequent calls in `patient.js` and `explorer.js` accessing `this.localLedger.transactions` crash with:
   ```text
   TypeError: Cannot read properties of undefined (reading 'transactions')
   ```
3. Patient Portal and Birds-Eye Explorer fail to render.

### How to Fix
In `application/src/services/api.js` (inside `initLocalLedger`):
```diff
- recipient: DEMO_ACCOUNTS.doctor_rajesh.address,
+ recipient: DEMO_ACCOUNTS.hospital.doctors[0].address,
```
Or in `application/src/services/state.js`, export an alias:
```javascript
DEMO_ACCOUNTS.doctor_rajesh = DEMO_ACCOUNTS.hospital.doctors[0];
```

---

## 📡 Bug #2: API Endpoints & Request Schema Mismatches

The frontend application uses custom `/api/...` routes that do not match the C++ node's binary-backed REST specification defined in [blockchain/API.md](blockchain/API.md).

### Endpoint Mapping Table

| Operation | What App Currently Calls (Fails with 404) | What C++ Node Exposes (Working) |
| :--- | :--- | :--- |
| **Check Node Status** | `${this.baseUrl}/api/status` | `GET /api/v1/chain/status` |
| **Get All Blocks** | `${this.baseUrl}/api/blocks` | `GET /api/v1/chain/blocks` |
| **Submit New Record** | `POST ${this.baseUrl}/api/records` | `POST /api/v1/blob/store` |
| **Grant Temporal Token** | `POST ${this.baseUrl}/api/keys/temporal` | `POST /api/v1/token/grant` |
| **Revoke Token** | *Local memory only (no HTTP call)* | `POST /api/v1/token/revoke` |
| **Doctor Decrypt Record** | *Not implemented in API client* | `POST /api/v1/token/audit_decrypt` |
| **Mine Pending Transactions**| *Not implemented in UI* | `POST /api/v1/node/mine` |
| **Get Specific Blob** | *Not implemented in API client* | `GET /api/v1/blob/:blob_id` |

---

## 🛠️ Step-by-Step Payload Fixes for `application/src/services/api.js`

### 1. Fix `submitRecord` (Commit Encrypted Blob to Mempool)
The C++ node expects a structured `payload` object with hex-encoded IV, tag, and base64 ciphertext:

```javascript
async submitRecord(recordPayload, patientAddress) {
  const now = Math.floor(Date.now() / 1000);
  const blobId = '0x' + (await ClientCrypto.sha256(recordPayload.ciphertext));

  // Canonical payload expected by C++ node /api/v1/blob/store
  const nodeTx = {
    sender: patientAddress,
    nonce: Date.now(),
    payload: {
      blob_id: blobId,
      previous_blob_id: '0x0000000000000000000000000000000000000000000000000000000000000000',
      owner_address: patientAddress,
      iv_hex: recordPayload.iv_hex || ClientCrypto.base64ToHex(recordPayload.iv),
      tag_hex: recordPayload.tag_hex || ClientCrypto.base64ToHex(recordPayload.tag),
      data_b64: recordPayload.ciphertext
    }
  };

  if (await this.checkConnection()) {
    try {
      const res = await fetch(`${this.getBaseUrl()}/api/v1/blob/store`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(nodeTx)
      });
      if (res.ok) {
        // Mine the block so it immediately appears on chain
        await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}'
        });
        return await res.json();
      }
    } catch (err) {
      console.warn('Failed to submit to node, falling back to local:', err);
    }
  }

  // Fallback to local ledger...
}
```

### 2. Fix `grantTemporalKey` (Issue Access Token)
The C++ node expects:
```javascript
async grantTemporalKey(grantData) {
  const now = Math.floor(Date.now() / 1000);
  const tokenId = '0x' + (await ClientCrypto.sha256('token-' + now + grantData.recipient));

  const nodePayload = {
    sender: grantData.sender,
    nonce: Date.now(),
    payload: {
      token_id: tokenId,
      target_blob_id: grantData.record_hash,
      grantor_address: grantData.sender,
      recipient_address: grantData.recipient,
      valid_from: grantData.valid_from || now,
      valid_until: grantData.valid_until,
      encrypted_symkey_hex: grantData.payload.authorized_document_sym_key_hex || ''
    }
  };

  if (await this.checkConnection()) {
    try {
      const res = await fetch(`${this.getBaseUrl()}/api/v1/token/grant`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(nodePayload)
      });
      if (res.ok) {
        await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}'
        });
        return await res.json();
      }
    } catch (err) {
      console.warn('Node token grant failed:', err);
    }
  }

  // Fallback to local ledger...
}
```

### 3. Implement On-Chain `revokeTemporalKey`
Currently, `revokeTemporalKey` only modifies an in-memory dictionary. Add the real API call:
```javascript
async revokeTemporalKey(keyId, senderAddress) {
  if (await this.checkConnection()) {
    try {
      const res = await fetch(`${this.getBaseUrl()}/api/v1/token/revoke`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          sender: senderAddress || DEMO_ACCOUNTS.patient.address,
          nonce: Date.now(),
          payload: {
            target_token_id: keyId,
            reason: 'Patient revoked access early'
          }
        })
      });
      if (res.ok) {
        await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: '{}'
        });
      }
    } catch (err) {
      console.warn('On-chain token revocation failed:', err);
    }
  }

  this.localLedger.revokedKeys[keyId] = true;
  appState.temporalKeys = appState.temporalKeys.filter(k => k.tx_id !== keyId);
  appState.notify();
  return { success: true };
}
```

### 4. Implement `auditDecrypt` (When Doctor Views Record)
In [hospital.js](application/src/views/hospital.js), when a doctor requests document access:
```javascript
async requestDecryption(tokenId, doctorAddress) {
  const res = await fetch(`${this.getBaseUrl()}/api/v1/token/audit_decrypt`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({
      token_id: tokenId,
      accessor_address: doctorAddress,
      timestamp: Math.floor(Date.now() / 1000)
    })
  });

  if (res.status === 403) {
    throw new Error('Access Denied: Temporal token is expired or revoked.');
  }

  const data = await res.json();
  // Automatically mines block to seal the audit receipt on-chain
  await fetch(`${this.getBaseUrl()}/api/v1/node/mine`, { method: 'POST', body: '{}' });

  return data.encrypted_symkey_hex;
}
```

### 5. Fix URL Inconsistency (`this.baseUrl` vs `this.getBaseUrl()`)
In several methods in `application/src/services/api.js`, code calls `${this.baseUrl}/...` instead of `${this.getBaseUrl()}/...`.
Because `this.baseUrl` is `null` when initialized via `ConfigManager`, requests resolve to `null/api/...`, causing network failures.
Replace all instances of `this.baseUrl` with `this.getBaseUrl()`.

---

## 🧪 Verification After Applying Fixes

1. Rebuild the app distribution:
   ```bash
   cd application
   node build.js
   ```
2. Start the local server:
   ```bash
   node build.js --serve 3000
   ```
3. Open `http://localhost:3000` in the browser:
   * Patient Portal and Birds-Eye Explorer will render without JavaScript console errors.
   * Creating a record will post directly to `http://localhost:8080/api/v1/blob/store` and seal a block on the live blockchain.
   * The real-time tracer (`sanjeev_trace --chain --follow`) will stream the newly created blocks in real time.
