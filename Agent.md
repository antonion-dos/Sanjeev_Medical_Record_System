# Sanjeev: Agent Guidelines & Architecture Manual

Welcome to **Sanjeev** — a blockchain-based patient records and integrated medical buddy system.

---

## ⚠️ STRICT AGENT RULES & GOVERNANCE

All AI agents, coding assistants, and automated contributors working on this repository MUST strictly abide by the following non-negotiable rules:

### 1. Scope Isolation (Strict Domain Boundary)
- **Agents are only allowed to modify EITHER the Application layer OR the Blockchain layer — specifically the single domain designated by the user in their request.**
- If the user request pertains to the **Blockchain**, the agent MUST NOT edit, delete, or create files inside `application/`.
- If the user request pertains to the **Application**, the agent MUST NOT edit, delete, or create files inside `blockchain/`.
- Cross-domain changes must only occur if the user explicitly orders a full-system modification in the prompt. Even then, changes should be segregated into clean, modular commits.

### 2. Branching & PR Protocol (DO NOT PUSH DIRECTLY TO MAIN)
- **NEVER PUSH DIRECTLY TO `main`**.
- Any and all changes must be developed on a dedicated temporary branch:
  ```bash
  # Format: feature/<description> or fix/<description> or agent/<task-id>
  git checkout -b feature/your-feature-name
  ```
- All commits must be clean, atomic, and well-described.
- Pushes must target the temporary branch and be accompanied by a Pull Request (PR) for review:
  ```bash
  git push origin feature/your-feature-name
  ```

---

## Repository Structure

```
Sanjeev/
├── Agent.md                        # This file: Agent governance rules and architecture guide
├── README.md                       # High-level overview, quick start, system specifications
├── .gitignore                      # Multi-language ignore rules (C++, CMake, Node, temp files)
│
├── blockchain/                     # DEDICATED DIRECTORY: C++ Blockchain & Node HTTP Server
│   ├── CMakeLists.txt              # Top-level CMake configuration (C++17, OpenSSL, pthreads)
│   ├── include/                    # Public C++ header files
│   │   ├── crypto.hpp              # SHA-256, AES-256-GCM, ECDSA keypairs, signatures
│   │   ├── transaction.hpp         # Transaction models (DocRecord, TemporalKey, Delegation)
│   │   ├── block.hpp               # Block structure, Merkle root, authority signatures
│   │   ├── blockchain.hpp          # Ledger state, memory pool, lineage tracker
│   │   ├── consensus.hpp           # Multi-authority PoA consensus (Government institutions)
│   │   └── server.hpp              # Built-in multithreaded REST/JSON HTTP server
│   ├── src/                        # C++ source implementations
│   │   ├── crypto.cpp              # OpenSSL cryptographic routines
│   │   ├── transaction.cpp         # Transaction serialization, verification, multi-sig
│   │   ├── block.cpp               # Block serialization, validation
│   │   ├── blockchain.cpp          # State engine, temporal key validator, query endpoints
│   │   ├── consensus.cpp           # Round-robin / M-of-N authority block signer
│   │   ├── server.cpp              # POSIX socket HTTP REST API handler
│   │   └── main.cpp                # Node CLI daemon entrypoint
│   └── tests/                      # Automated C++ test suites
│       ├── CMakeLists.txt
│       ├── test_crypto.cpp         # Encryption, hashing, and signature tests
│       ├── test_temporal_keys.cpp  # Key expiration, access delegation, and validation tests
│       └── test_poa.cpp            # Authority consensus and multi-sig verification tests
│
└── application/                    # DEDICATED DIRECTORY: Web & Mobile Application
    ├── package.json                # Project scripts (build, dev, test)
    ├── build.js                    # Zero-dependency asset bundler & local dev server
    ├── public/                     # Public web assets
    │   ├── index.html              # Single Page Application HTML shell
    │   ├── manifest.json           # Mobile PWA manifest
    │   └── favicon.svg             # Application branding icon
    └── src/                        # Modular frontend source code (vanilla ES6+)
        ├── index.js                # App bootstrap & view coordinator (Patient / Hospital / Viewer)
        ├── styles/
        │   └── app.css             # Healthcare UI design system (accessible, responsive)
        ├── services/
        │   ├── api.js              # Blockchain HTTP REST client
        │   ├── crypto.js           # Client-side WebCrypto (AES-GCM encryption/decryption)
        │   └── openrouter.js       # OpenRouter API client for medical interpretation
        └── views/
            ├── patient.js          # Patient View: Records, Issue Temporal Key, LLM Buddy
            ├── hospital.js         # Hospital View: Doctor registry, Key delegation
            └── explorer.js         # Birds-eye Blockchain Viewer & Temporal Key lineage tracker
```

---

## Architectural Overview

### 1. Blockchain Layer (`blockchain/`)
- **Technology Stack**: C++17, CMake 3.16+, OpenSSL 3.x (`libcrypto` and `libssl`), POSIX sockets.
- **Encrypted Document Store**:
  - Medical records are encrypted before storage. The blockchain stores the ciphertext, initialization vector (IV), authentication tag, and cryptographic hash.
  - Plaintext medical data is never committed to the ledger.
- **Traceability & Chain of Custody**:
  - Every event (document submission, temporal key grant, key delegation, revocation) forms a verifiable link in the blockchain.
  - The node provides dedicated trace query APIs to construct full provenance trees.
- **MultiSignatory Protocol**:
  - Transactions support M-of-N threshold signatures (e.g. Patient + Physician consent, or multi-authority sign-off).
- **Temporal Access Keys (TAK)**:
  - Private key holders (patients) issue time-limited keys (`valid_from` to `valid_until` unix timestamps).
  - Keys can be scoped to specific documents or entire medical profiles.
  - The node state engine actively validates the temporal window before authorizing read/access requests.
- **Multi-Authority Consensus (PoA)**:
  - Blocks are authorized and mined by designated central institutions (e.g. Ministry of Health, Certified Medical Councils, Government Regulators).
  - Blocks lacking valid authority signatures are rejected.
- **Embedded HTTP REST Server**:
  - Lightweight, high-performance embedded HTTP server exposing JSON endpoints on port `8080` (or user-configured port).
  - Provides query and submission APIs for blocks, transactions, temporal keys, and lineage trees.

### 2. Application Layer (`application/`)
- **Framework Philosophy**: Zero heavy external frameworks (no heavy React/Angular/Vue dependencies) unless strictly justified. Built with modern, modular vanilla JavaScript (ES6+), standard Web Components, CSS custom properties, and Web Crypto API.
- **Dual-Mode Single Application**:
  - **Patient Mode**:
    - Manage encrypted medical records.
    - Issue time-bound Temporal Keys to Hospitals or Doctors.
    - Decrypt records locally and securely.
    - **LLM Medical Buddy**: Integrate with OpenRouter to send decrypted medical text to a model of the patient's choice (e.g., Claude 3.5 Sonnet, GPT-4o, Llama 3) for plain-language interpretation, clinical term explanation, and second opinions.
  - **Hospital Mode**:
    - Supports both **independent doctors** and **large hospital networks**.
    - Maintains a registry of received patient temporal keys.
    - Supports hierarchical key delegation: Hospital can securely route temporal keys to specific doctors within its organization.
    - Doctors inspect decrypted records only within the active time window.
- **Blockchain Viewer / Explorer**:
  - Full bird's-eye view of the entire blockchain.
  - Visual lineage tracker illustrating:
    `Patient Address -> Temporal Key (Time Limit) -> Hospital Address -> Delegated Doctor Address`
  - Instant visibility into active vs. expired permissions.

---

## Build & Run Runbooks

### Building the Blockchain
```bash
cd blockchain
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

### Running Blockchain Unit Tests
```bash
cd blockchain/build
ctest --output-on-failure
```

### Running the Blockchain Node
```bash
./blockchain/build/sanjeev_node --port 8080
```

### Building & Running the Application
```bash
cd application
# Build assets
node build.js
# Start local server on port 3000
node build.js --serve 3000
```
Then visit `http://localhost:3000` in any modern web browser or mobile browser.
