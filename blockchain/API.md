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

#### A. Trace Complete Block Ledger & Live Real-Time Follow
Inspects all sealed blocks, authorities, and transactions:
```bash
./build/sanjeev_trace --chain --db sanjeev_node.db
```
To stream newly sealed blocks, transaction updates, and on-chain decryption audit receipts **in real time**:
```bash
./build/sanjeev_trace --chain --db sanjeev_node.db --follow
```
**Sample Real-Time Live Stream:**
```text
===============================================================
   Sanjeev (संजीव) - Blockchain Transaction & Lineage Tracer   
===============================================================

[Ledger Summary]
  Total Blocks: 2
---------------------------------------------------------------
Block #  0 | Hash: 0x28023b04a7c803... | TxCount:  0 | Authority: Ministry of Health | Time: 1790838779
Block #  1 | Hash: 0xfb60bc638897b7... | TxCount:  2 | Authority: Ministry of Health | Time: 1790838779
   └─ [Tx BLOB_STORE]  ID: 0x09b66b69fee96d... | Sender: 0xba5c4622f65c...
   └─ [Tx TOKEN_GRANT] ID: 0xb680d4c6239c8c... | Sender: 0xba5c4622f65c...
---------------------------------------------------------------

[*] Watching ledger in REAL-TIME for new blocks, transactions, and audit receipts...
    (Press Ctrl+C to stop realtime tracer)

>>> [REAL-TIME LEDGER EVENT] New Block Sealed! <<<
  Block #2 | Hash: 0xed27d612192ad0bc753a1b0ff531616cb64baf08473227dd44ef43d713e634f1
  Authority: Government Ministry of Health - Authority Validator #1
  Transactions (1):
    └─ [Tx DECRYPTION_AUDIT] ID: 0x7af4d07b37246f... | Sender: 0x00267b5fefb3...
      • [DECRYPTION AUDIT] Accessor: 0x00267b5fefb3...
      • Accessed Blob: 0xc7d00c427df03fe694...
      • Token Used: 0x7a7a7a7a7a7a7a7a7a...
---------------------------------------------------------------
```

#### B. Trace Document Revision Provenance & Access Audit Receipts
Inspects a specific encrypted medical record to display its complete version history and every time an authorized party requested decryption:
```bash
./build/sanjeev_trace --blob 0x05a2e10f1e4575b28476... --db sanjeev_node.db
```
Or stream document access events live:
```bash
./build/sanjeev_trace --blob 0x05a2e10f1e4575b28476... --db sanjeev_node.db --follow
```
**Sample Output:**
```text
[Target Encrypted Blob Details]
  Blob ID        : 0x05a2e10f1e4575b28476c57ed49be5199ef406f76a549129cc4d49b613802b6f
  Previous Version: 0x3c2685c4bd85b1473eb5830d0f0dd9e59a543ac33da67d0bfb5f96c05ae525e6
  Owner Address  : 0x1122334455667788990011223344556677889900
  Updater Address: 0xa6d8eeb46acc53ff5eb04dbfd4d8e81d8a5fa65c
  Ciphertext Size: 155 bytes
  IV (Hex)       : aabbccddeeff001122334455
  Auth Tag (Hex) : 99887766554433221100ffeeddccbbaa
  Timestamp      : 1790839053

--- Version Provenance Chain (2 revisions) ---
  v2: 0x05a2e10f1e4575b28476c57ed49be5199ef406f76a549129cc4d49b613802b6f (Updated by: 0xa6d8eeb46acc... at t=1790839053)
  v1: 0x3c2685c4bd85b1473eb5830d0f0dd9e59a543ac33da67d0bfb5f96c05ae525e6 (Updated by: 0x112233445566... at t=1790838996)

--- On-Chain Decryption Audit Receipts (1 access events) ---
  [ACCESS EVENT] Accessor: 0xa6d8eeb46acc53ff5eb04dbfd4d8e81d8a5fa65c | Token: 0x88888888888888... | Time: 1790835000
```

#### C. Trace Temporal Access Token Status & Delegation
Inspects the cryptographic validity window and delegation parent for a token:
```bash
./build/sanjeev_trace --token 0x8888888888888888888888888888888888888888888888888888888888888888 --db sanjeev_node.db
```
**Sample Output:**
```text
[Temporal Access Token Information]
  Token ID       : 0x8888888888888888888888888888888888888888888888888888888888888888
  Target Blob ID : 0x3c2685c4bd85b1473eb5830d0f0dd9e59a543ac33da67d0bfb5f96c05ae525e6
  Grantor (Owner): 0x1122334455667788990011223344556677889900
  Recipient      : 0xa6d8eeb46acc53ff5eb04dbfd4d8e81d8a5fa65c
  Valid Window   : 1790830000 -> 1790930000
  Delegation Parent: Root Grant (Direct from Patient)
  Status Code    : 2 (REVOKED)
```

#### D. Inspect Specific Transaction Payload
```bash
./build/sanjeev_trace --tx 0x2d6084df39ed1734100901e956ce2c2b6589725c37dfaa1a853fb3f4afc32de2 --db sanjeev_node.db
```

---

## 📦 Multi-File Document Encryption Pattern (ZIP Archive)

In healthcare workflows, patient records rarely consist of a single text file; they contain clinical summaries and diagnostic imaging (e.g. DICOM/PNG scans). Sanjeev handles this through encrypted container archives:

1. **Client Bundling**: The client application packages multiple files (e.g. `medical_scan.png` and `clinical_report.txt`) into a single uncompressed ZIP archive byte stream.
2. **AES-256-GCM Encryption**: The client generates a random 256-bit symmetric key and encrypts the entire ZIP archive.
3. **On-Chain Commitment**: The resulting ciphertext, IV, tag, and SHA-256 hash are committed via `BLOB_STORE`. The blockchain never inspects the inner contents.
4. **Temporal Delegation**: The patient issues a `TOKEN_GRANT` wrapping the symmetric key for the recipient doctor or hospital, bounded by `valid_from` and `valid_until`.
5. **Decryption & Unpacking**: When the doctor requests decryption within the valid window, an audit event is logged on-chain, the symmetric key is released, and the client decrypts and extracts both the scan and report byte-for-byte.

