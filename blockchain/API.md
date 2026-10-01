# Sanjeev Blockchain API & Subsystem Reference

This document outlines the architecture, interaction patterns, and HTTP REST / Binary protocol for the **Sanjeev** decentralized health ledger.

---

## 📊 Component Readiness Dashboard

| Component | Subsystem | Responsibility | Status |
| :--- | :--- | :--- | :---: |
| **1. Binary Protocol** | `binary_buffer.hpp` | Endian-safe serialization/deserialization for canonical hashing | 🟡 In Progress |
| **2. Core Data Structures** | `types.hpp`, `transaction.*`, `block.*` | Encrypted blobs, temporal tokens, audit records, blocks | 🟡 In Progress |
| **3. Cryptographic Suite** | `crypto.*` | OpenSSL ECDSA (secp256k1/P-256), AES-256-GCM, ECDH key agreement | 🔴 Pending Refactor |
| **4. Persistence Engine** | `storage.*` | Embedded SQLite storage for blocks, tokens, and lineage | 🔴 Pending |
| **5. State & Lineage Machine**| `blockchain.*` | Temporal validity enforcement, mempool, provenance tracing | 🔴 Pending |
| **6. PoA Consensus** | `consensus.*` | Institutional authority block signing and threshold verification | 🔴 Pending |
| **7. Cross-Platform API Server**| `server.*` | REST JSON/Binary API over Winsock/POSIX sockets | 🔴 Pending |
| **8. Node Daemon & Tests** | `main.cpp`, `tests/` | CLI daemon, automated unit and integration test suite | 🔴 Pending |

**Overall Completion Estimate:** ~20% (Core architecture and specs established; binary buffer and core data types in progress).

---

## 🔐 Core Concepts

1. **Agnostic Encrypted Blobs**: The blockchain stores encrypted medical documents as opaque byte arrays. The ledger verifies ownership, timestamps, and hashes without knowing whether the content is a prescription, lab report, or scan.
2. **Dual-Function User Keypairs**: Every actor (Patient, Hospital, Doctor) holds an elliptic-curve keypair used for:
   - **Identity & Signing**: ECDSA signature on transactions and access grants.
   - **Key Agreement & Decryption**: ECDH/ECIES encryption of symmetric document keys.
3. **Temporal Access Tokens**: Time-limited capabilities (`valid_from` to `valid_until`) issued by patients and delegatable by hospitals to specific attending doctors.
4. **Decryption Audit Trail**: Every access attempt or decryption request creates an on-chain audit receipt, making unauthorized or off-time access verifiably detectable.

---

## 🌐 HTTP REST API Specification

### Base URL: `http://<node-host>:8080/api/v1`

### 1. Blobs (Encrypted Records)

#### `POST /blob/store`
Commits a new encrypted medical record to the mempool.
* **Payload:**
  ```json
  {
    "sender": "0x4a9b...20bytes",
    "timestamp": 1743510000,
    "nonce": 1,
    "payload": {
      "blob_id": "0x3f2a...32bytes",
      "owner_address": "0x4a9b...20bytes",
      "iv_hex": "a1b2c3d4e5f60718293a4b5c",
      "tag_hex": "9f8e7d6c5b4a39281726354455667788",
      "data_b64": "<base64-encoded-ciphertext>"
    },
    "signatures": [
      { "signer": "0x4a9b...20bytes", "pubkey_hex": "...", "signature_hex": "..." }
    ]
  }
  ```
* **Response:** `200 OK` `{ "status": "accepted", "tx_id": "0x...", "blob_id": "0x..." }`

#### `POST /blob/update`
Commits a new version of an existing blob, preserving provenance.
* **Payload:**
  ```json
  {
    "sender": "0x4a9b...20bytes",
    "timestamp": 1743510500,
    "nonce": 2,
    "payload": {
      "previous_blob_id": "0x3f2a...32bytes",
      "new_blob": {
        "blob_id": "0x7c8d...32bytes",
        "owner_address": "0x4a9b...20bytes",
        "updater_address": "0x98fe...20bytes",
        "iv_hex": "...",
        "tag_hex": "...",
        "data_b64": "..."
      }
    },
    "signatures": [...]
  }
  ```

#### `GET /blob/:blob_id`
Retrieves the encrypted blob data, current version, and updater history.

---

### 2. Temporal Access Tokens

#### `POST /token/grant`
Patient issues a time-bound access grant to a hospital or doctor.
* **Payload:**
  ```json
  {
    "sender": "0xPatientAddress",
    "timestamp": 1743510000,
    "payload": {
      "token_id": "0xTokenIdHex",
      "target_blob_id": "0xBlobIdHex",
      "grantor_address": "0xPatientAddress",
      "recipient_address": "0xHospitalAddress",
      "valid_from": 1743510000,
      "valid_until": 1743596400,
      "encrypted_symkey_hex": "..."
    },
    "signatures": [...]
  }
  ```

#### `POST /token/delegate`
Hospital delegates an active token to an attending physician.
* **Payload:**
  ```json
  {
    "sender": "0xHospitalAddress",
    "payload": {
      "parent_token_id": "0xParentTokenIdHex",
      "child_token": {
        "token_id": "0xChildTokenIdHex",
        "target_blob_id": "0xBlobIdHex",
        "recipient_address": "0xDoctorAddress",
        "valid_from": 1743510000,
        "valid_until": 1743550000,
        "encrypted_symkey_hex": "..."
      }
    },
    "signatures": [...]
  }
  ```

#### `POST /token/revoke`
Grantor revokes a token immediately before its expiration timestamp.

---

### 3. Decryption Auditing

#### `POST /token/audit_decrypt`
Records on-chain that an address requested/accessed decryption of a blob under an active token.
* **Payload:**
  ```json
  {
    "sender": "0xAccessorAddress",
    "payload": {
      "token_id": "0xTokenIdHex",
      "blob_id": "0xBlobIdHex",
      "accessor_address": "0xAccessorAddress",
      "access_timestamp": 1743512000
    },
    "signatures": [...]
  }
  ```

---

### 4. Ledger & Lineage Queries

* `GET /chain/status` - Node sync state, current block height, authority list.
* `GET /chain/blocks?limit=10&offset=0` - Query historical blocks.
* `GET /lineage/:id` - Full provenance tree connecting Patient $\rightarrow$ Blob $\rightarrow$ Token $\rightarrow$ Hospital $\rightarrow$ Doctor $\rightarrow$ Decryption Audits.
