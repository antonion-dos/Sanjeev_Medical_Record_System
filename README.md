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

### 1. Build & Run the Blockchain
```bash
cd blockchain
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Run test suite
ctest --output-on-failure

# Start the node & HTTP REST server
./sanjeev_node --port 8080
```

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
