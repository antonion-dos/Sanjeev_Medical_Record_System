# Sanjeev Blockchain API & Subsystem Reference

This document outlines the architecture, interaction patterns, and HTTP REST / Binary protocol for the **Sanjeev** decentralized health ledger.

---

## 📊 Component Readiness Dashboard

| Component | Subsystem | Responsibility | Status |
| :--- | :--- | :--- | :---: |
| **1. Binary Protocol** | `binary_buffer.hpp` | Endian-safe serialization/deserialization for canonical hashing | 🟢 **Ready** (100%) |
| **2. Core Data Structures** | `types.hpp`, `transaction.*`, `block.*` | Encrypted blobs, temporal tokens, audit records, Merkle tree | 🟢 **Ready** (100%) |
| **3. Cryptographic Suite** | `crypto.*` | OpenSSL ECDSA (secp256k1/P-256), AES-256-GCM, DER signatures | 🟢 **Ready** (100%) |
| **4. Persistence Engine** | `storage.*` | Embedded SQLite storage for blocks, blobs, tokens, and audits | 🟢 **Ready** (100%) |
| **5. State & Lineage Machine**| `blockchain.*` | Temporal validity enforcement, mempool, provenance tracing | 🟢 **Ready** (100%) |
| **6. PoA Consensus** | `consensus.*` | Institutional authority block signing and threshold verification | 🟢 **Ready** (100%) |
| **7. Cross-Platform API Server**| `server.*` | REST JSON API over Winsock/POSIX sockets | 🟢 **Ready** (100%) |
| **8. Node Daemon & Tests** | `main.cpp`, `CMakeLists.txt`, `tests/` | CLI daemon, CMake Ninja build, CTest automated test suite | 🟢 **Ready** (100%) |

**Overall Completion Estimate:** **100%** (All 8 subsystems implemented, integrated with SQLite, compiled with CMake/Ninja, and 100% verified with automated tests).

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
* `GET /chain/blocks` - Query historical blocks, transaction IDs, types, and authority seals.
* `GET /blob/:blob_id` - Fetch encrypted blob details, IV, tag, and previous version ID.
* `GET /lineage/:id` - Full provenance tree connecting Patient $\rightarrow$ Blob $\rightarrow$ Token $\rightarrow$ Hospital $\rightarrow$ Doctor $\rightarrow$ Decryption Audits.

---

## 🔍 How to View and Trace Transactions

Sanjeev provides two complementary interfaces for auditing and tracing transactions: the **HTTP REST Endpoints** and the native **CLI Trace Tool (`sanjeev_trace`)**.

### 1. Using the CLI Trace Tool (`sanjeev_trace`)

The `sanjeev_trace` binary allows administrators, auditors, and patients to directly query the local or node SQLite ledger without external dependencies.

#### A. View Full Ledger Chain & Transaction Overview
```bash
./build/sanjeev_trace --chain --db sanjeev_node.db
```
**Sample Output:**
```text
===============================================================
   Sanjeev (संजीव) - Blockchain Transaction & Lineage Tracer   
===============================================================

[Ledger Summary]
  Total Blocks: 2
---------------------------------------------------------------
Block #  0 | Hash: 0xeeb374a5b9a1c2... | TxCount:  0 | Authority: Ministry of Health | Time: 1790837750
Block #  1 | Hash: 0xb1243c9d4c320e... | TxCount:  2 | Authority: Ministry of Health | Time: 1790837750
   └─ [Tx BLOB_STORE]  ID: 0x039d9ec037b969... Sender: 0x65c5d8f342...
   └─ [Tx TOKEN_GRANT] ID: 0xb187f14500b316... Sender: 0x65c5d8f342...
---------------------------------------------------------------
```

#### B. Trace Document Revision Provenance & Access Audit Receipts
Inspects a specific encrypted medical record to display its complete version history and every time an authorized party requested decryption:
```bash
./build/sanjeev_trace --blob 0x039d9ec037b96942... --db sanjeev_node.db
```
**Sample Output:**
```text
[Target Encrypted Blob Details]
  Blob ID        : 0x039d9ec037b96942...
  Previous Version: None (Genesis Version)
  Owner Address  : 0x65c5d8f342b30b42fcf023d6...
  Updater Address: 0x65c5d8f342b30b42fcf023d6...
  Ciphertext Size: 1048 bytes
  IV (Hex)       : a1b2c3d4e5f60718293a4b5c
  Auth Tag (Hex) : 9f8e7d6c5b4a39281726354455667788
  Timestamp      : 1790837750

--- Version Provenance Chain (1 revisions) ---
  v1: 0x039d9ec037b96942... (Updated by: 0x65c5d8f342... at t=1790837750)

--- On-Chain Decryption Audit Receipts (1 access events) ---
  [ACCESS EVENT] Accessor: 0x47e19f2a08... | Token: 0xb187f14500b316... | Time: 1790838100
```

#### C. Trace Temporal Access Token Status & Delegation
Inspects the cryptographic validity window and delegation parent for a token:
```bash
./build/sanjeev_trace --token 0xb187f14500b31671... --db sanjeev_node.db
```

#### D. Inspect Specific Transaction Payload
```bash
./build/sanjeev_trace --tx 0x039d9ec037b96942... --db sanjeev_node.db
```

---

## 📦 Multi-File Document Encryption Pattern (ZIP Archive)

In healthcare workflows, patient records rarely consist of a single text file; they contain clinical summaries and diagnostic imaging (e.g. DICOM/PNG scans). Sanjeev handles this through encrypted container archives:

1. **Client Bundling**: The client application packages multiple files (e.g. `medical_scan.png` and `clinical_report.txt`) into a single uncompressed ZIP archive byte stream.
2. **AES-256-GCM Encryption**: The client generates a random 256-bit symmetric key and encrypts the entire ZIP archive.
3. **On-Chain Commitment**: The resulting ciphertext, IV, tag, and SHA-256 hash are committed via `BLOB_STORE`. The blockchain never inspects the inner contents.
4. **Temporal Delegation**: The patient issues a `TOKEN_GRANT` wrapping the symmetric key for the recipient doctor or hospital, bounded by `valid_from` and `valid_until`.
5. **Decryption & Unpacking**: When the doctor requests decryption within the valid window, an audit event is logged on-chain, the symmetric key is released, and the client decrypts and extracts both the scan and report byte-for-byte.

