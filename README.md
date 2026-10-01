# Sanjeev (संजीव)
> Blockchain-Based Patient Records & Integrated Medical Buddy System

Sanjeev is a decentralized, cryptographically secure health records ecosystem with zero-knowledge-oriented data privacy, institutional multi-authority consensus, time-limited access delegation, and an AI-powered medical buddy for patient health literacy.

---

## 🏛️ System Architecture

The project is structured into two dedicated directories:

```
Sanjeev/
├── blockchain/         # C++ Custom Blockchain with PoA consensus & embedded HTTP API
└── application/        # Zero-framework Web & Mobile App (Patient & Hospital/Doctor)
```

### 1. Blockchain Layer (`blockchain/`)
- **Engine**: Custom distributed ledger implemented in modern C++17 with CMake.
- **Encrypted Document Store**: Patient medical documents are encrypted via AES-256-GCM before ledger inclusion. The blockchain securely preserves document digests, IVs, tags, and encrypted envelopes.
- **Traceability**: Cryptographic audit trails track document creation, temporal key issuance, doctor delegation, and time expiry.
- **MultiSignatory**: Transactions support M-of-N threshold signatures for institutional co-signing and patient-physician mutual consent.
- **Temporal Access Keys (TAK)**: Time-limited access grants (`valid_from` to `valid_until`) cryptographically signed by the patient, verifiable on-chain.
- **Authority Consensus**: Proof of Authority (PoA) where authorized central authorities (e.g., Ministry of Health, Certified Medical Councils) validate and seal blocks.
- **Built-in HTTP REST Server**: Native C++ server exposing endpoints for block queries, transaction submission, temporal key checks, and lineage tracing.

### 2. Application Layer (`application/`)
- **Framework Philosophy**: Zero heavy frameworks unless justified. Pure modern vanilla JavaScript (ES6+), HTML5, and CSS3. Fast, lightweight, and deployable as a Progressive Web App (PWA) or wrapped mobile app.
- **Dual-Role Interface**:
  - **Patient Mode**: View owned records, issue time-bound temporal keys to hospitals/doctors, audit access history, and use the LLM Medical Buddy.
  - **Hospital Mode**: Scalable from an independent physician to a multi-specialty hospital. Manages patient temporal keys and delegates keys hierarchically to doctors.
- **LLM Medical Buddy (OpenRouter)**:
  - Secure client-side decryption (medical data remains private to the user's browser).
  - OpenRouter integration allowing the patient to pick their preferred AI model (Claude 3.5 Sonnet, GPT-4o, Llama 3, Mistral, etc.) to interpret complex medical reports into clear, empathetic explanations.
- **Blockchain Viewer / Explorer**:
  - Birds-eye view of all blocks and transactions.
  - Interactive lineage flow tracking: `Patient Address -> Temporal Key -> Hospital -> Doctor`.

---

## 🚀 Quick Start

### Prerequisites
- GCC 10+ or Clang 11+
- CMake 3.16+
- OpenSSL 3.x
- Node.js 18+ (used strictly for build bundling and local static serving)

### 1. Build & Run the Blockchain Node

```bash
cd blockchain
mkdir -p build && cd build
cmake ..
cmake --build .

# Run automated edge-case and core test suites (6/6 suites)
ctest --output-on-failure

# Start the node & HTTP REST server
./sanjeev_node --port 8080 --db sanjeev_node.db --name "Ministry of Health Validator #1"
```
The node starts an HTTP REST server listening on `http://localhost:8080` backed by an SQLite WAL database.

#### Verifying Node Health & Ledger via REST API

In PowerShell or bash, query the node's live consensus state:

```bash
# Check node status, block height, and mempool
curl -s http://localhost:8080/api/v1/chain/status

# Inspect mined blocks, hashes, and Merkle roots
curl -s http://localhost:8080/api/v1/chain/blocks

# Mine pending transactions in mempool into a new block
curl -X POST http://localhost:8080/api/v1/node/mine -H "Content-Type: application/json" -d "{}"
```

#### Real-Time Verification via CLI Trace Tool (`sanjeev_trace`)

Keep the tracer open in a separate terminal to watch new blocks, transactions, and on-chain decryption audit receipts streamed live as they occur:

```bash
# Live follow mode (streams blocks and audit receipts in real-time)
./sanjeev_trace --chain --db sanjeev_node.db --follow

# Audit specific document revision history and decryption events
./sanjeev_trace --blob <blob_id_hex> --db sanjeev_node.db

# Inspect temporal access token validity and revocation status
./sanjeev_trace --token <token_id_hex> --db sanjeev_node.db
```

> For full REST endpoint schemas and payload structures, see [blockchain/API.md](blockchain/API.md).
> For the comprehensive zero-knowledge consensus specification, see [blockchain/BLOCKCHAIN_EXPLAINED.txt](blockchain/BLOCKCHAIN_EXPLAINED.txt).

### 2. Build & Run the Application
```bash
cd application

# Build bundle
node build.js

# Start application server
node build.js --serve 3000
```
Open [http://localhost:3000](http://localhost:3000) in your browser.

---

## 📜 Agent Guidelines & Contributing

Please consult [`Agent.md`](Agent.md) before making contributions. Note the mandatory rules:
1. **Domain Scoping**: Agents must exclusively modify either `blockchain/` OR `application/`, as specified by the user.
2. **Branching**: Never push directly to `main`. Always create a temporary branch and submit a PR.
