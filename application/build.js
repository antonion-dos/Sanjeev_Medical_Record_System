#!/usr/bin/env node

/**
 * Sanjeev Application Zero-Dependency Builder & Local Server
 * Adheres strictly to "No frameworks unless justified".
 * Uses pure Node.js standard libraries (fs, path, http, url).
 */

const fs = require('fs');
const path = require('path');
const http = require('http');

const ROOT_DIR = __dirname;
const SRC_DIR = path.join(ROOT_DIR, 'src');
const PUBLIC_DIR = path.join(ROOT_DIR, 'public');
const DIST_DIR = path.join(ROOT_DIR, 'dist');

// MIME types dictionary for static serving
const MIME_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.ico': 'image/x-icon'
};

function copyDirRecursive(src, dest) {
  if (!fs.existsSync(dest)) {
    fs.mkdirSync(dest, { recursive: true });
  }
  const entries = fs.readdirSync(src, { withFileTypes: true });
  for (const entry of entries) {
    const srcPath = path.join(src, entry.name);
    const destPath = path.join(dest, entry.name);
    if (entry.isDirectory()) {
      copyDirRecursive(srcPath, destPath);
    } else {
      fs.copyFileSync(srcPath, destPath);
    }
  }
}

function build() {
  console.log('[Build] Packaging Sanjeev Application into dist/...');
  const startTime = Date.now();

  // Create clean dist directory
  if (fs.existsSync(DIST_DIR)) {
    fs.rmSync(DIST_DIR, { recursive: true, force: true });
  }
  fs.mkdirSync(DIST_DIR, { recursive: true });

  // Copy public assets
  copyDirRecursive(PUBLIC_DIR, DIST_DIR);

  // Copy src directory
  copyDirRecursive(SRC_DIR, path.join(DIST_DIR, 'src'));

  // Adjust index.html paths in dist/
  const indexPath = path.join(DIST_DIR, 'index.html');
  if (fs.existsSync(indexPath)) {
    let indexHtml = fs.readFileSync(indexPath, 'utf-8');
    indexHtml = indexHtml.replace('../src/styles/app.css', './src/styles/app.css');
    indexHtml = indexHtml.replace('../src/index.js', './src/index.js');
    fs.writeFileSync(indexPath, indexHtml);
  }

  const duration = Date.now() - startTime;
  console.log(`[Build] Done in ${duration}ms. Distribution generated at: ${DIST_DIR}`);
}

let DatabaseSync = null;
try {
  DatabaseSync = require('node:sqlite').DatabaseSync;
} catch (e) {
  DatabaseSync = null;
}

const DB_PATH = path.resolve(ROOT_DIR, '../blockchain/sanjeev_live.db');
const ENTITIES_FILE = path.join(ROOT_DIR, 'entities.json');

function loadRegisteredEntities() {
  if (fs.existsSync(ENTITIES_FILE)) {
    try {
      return JSON.parse(fs.readFileSync(ENTITIES_FILE, 'utf-8'));
    } catch (e) {
      return [];
    }
  }
  return [];
}

function saveRegisteredEntities(entities) {
  try {
    fs.writeFileSync(ENTITIES_FILE, JSON.stringify(entities, null, 2), 'utf-8');
  } catch (e) {
    console.error('Failed to save entities:', e);
  }
}

function postToBlockchainNode(pathname, data) {
  return new Promise((resolve, reject) => {
    const payload = JSON.stringify(data);
    const req = http.request({
      hostname: 'localhost',
      port: 8080,
      path: pathname,
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Content-Length': Buffer.byteLength(payload)
      }
    }, (res) => {
      let body = '';
      res.on('data', chunk => body += chunk);
      res.on('end', () => {
        try {
          resolve({ status: res.statusCode, data: JSON.parse(body || '{}') });
        } catch {
          resolve({ status: res.statusCode, data: body });
        }
      });
    });
    req.on('error', reject);
    req.write(payload);
    req.end();
  });
}

async function processExternalEntity(payload) {
  const now = Math.floor(Date.now() / 1000);
  const crypto = require('crypto');

  const patientName = payload.patient_name || 'Vikram Malhotra';
  const patientAddress = (payload.patient_address || '0x4a9b6c7d8e1234567890abcdef1234567890abcd').toLowerCase();
  
  const hospitalName = payload.hospital_name || 'Max Super Speciality Hospital';
  const hospitalAddress = (payload.hospital_address || '0x5b8c7d8e9f2345678901bcdef12345678901bcde').toLowerCase();

  const doctorName = payload.doctor_name || 'Dr. Ananya Roy, MD';
  const doctorSpecialty = payload.doctor_specialty || 'Senior Neurosurgeon';
  const doctorAddress = (payload.doctor_address || '0x6c9d8e9fa03456789012cdef123456789012cdef').toLowerCase();

  const recordTitle = payload.record_title || 'Neurological Evaluation & Brain MRI Scan';
  const department = payload.department || 'Neurology';
  const durationHours = parseInt(payload.duration_hours, 10) || 24;

  const blobId = '0x' + crypto.randomBytes(32).toString('hex');
  const tokenId = '0x' + crypto.randomBytes(32).toString('hex');
  const symKeyHex = crypto.randomBytes(32).toString('hex');
  const ivHex = crypto.randomBytes(12).toString('hex');
  const tagHex = crypto.randomBytes(16).toString('hex');
  const samplePlaintext = JSON.stringify({
    title: recordTitle,
    patient_name: patientName,
    department: department,
    hospital: hospitalName,
    attending_doctor: doctorName,
    timestamp: now,
    status: 'Verified Sovereign Record'
  });
  const dataB64 = Buffer.from(samplePlaintext).toString('base64');

  // 1. Submit BLOB_STORE to C++ Node
  const blobRes = await postToBlockchainNode('/api/v1/blob/store', {
    sender: patientAddress,
    nonce: Date.now(),
    payload: {
      blob_id: blobId,
      previous_blob_id: '0x0000000000000000000000000000000000000000000000000000000000000000',
      owner_address: patientAddress,
      iv_hex: ivHex,
      tag_hex: tagHex,
      data_b64: dataB64
    }
  });

  // 2. Mine block to seal blob
  const mineRes1 = await postToBlockchainNode('/api/v1/node/mine', {});

  // 3. Submit TOKEN_GRANT from Patient to Hospital
  const grantRes = await postToBlockchainNode('/api/v1/token/grant', {
    sender: patientAddress,
    nonce: Date.now() + 1,
    payload: {
      token_id: tokenId,
      target_blob_id: blobId,
      grantor_address: patientAddress,
      recipient_address: hospitalAddress,
      valid_from: now,
      valid_until: now + (durationHours * 3600),
      encrypted_symkey_hex: symKeyHex
    }
  });

  // 4. Mine block to seal token grant
  const mineRes2 = await postToBlockchainNode('/api/v1/node/mine', {});

  // 5. Doctor performs Decryption Audit
  const auditRes = await postToBlockchainNode('/api/v1/token/audit_decrypt', {
    token_id: tokenId,
    accessor_address: hospitalAddress,
    timestamp: now
  });

  // 6. Mine block to seal audit
  const mineRes3 = await postToBlockchainNode('/api/v1/node/mine', {});

  // 7. Save Entity Metadata
  const entities = loadRegisteredEntities();
  const entityRecord = {
    id: 'ent-' + Date.now(),
    timestamp: now,
    patient: {
      name: patientName,
      address: patientAddress,
      role: 'patient'
    },
    hospital: {
      name: hospitalName,
      address: hospitalAddress,
      role: 'hospital'
    },
    doctor: {
      name: doctorName,
      specialty: doctorSpecialty,
      address: doctorAddress,
      role: 'doctor'
    },
    record: {
      title: recordTitle,
      department: department,
      blob_id: blobId
    },
    token: {
      token_id: tokenId,
      valid_from: now,
      valid_until: now + (durationHours * 3600),
      duration_hours: durationHours,
      sym_key_hex: symKeyHex
    }
  };
  entities.push(entityRecord);
  saveRegisteredEntities(entities);

  return {
    success: true,
    message: 'New Patient -> Hospital -> Doctor entity committed to blockchain and registered in app.',
    entity: entityRecord,
    onchain: {
      blob_tx: blobRes.data,
      grant_tx: grantRes.data,
      audit: auditRes.data,
      latest_block: mineRes3.data
    }
  };
}

function serve(port = 3000) {
  build();

  const server = http.createServer((req, res) => {
    // Enable CORS
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }

    const parsedUrl = new URL(req.url, `http://${req.headers.host || 'localhost'}`);
    const pathname = parsedUrl.pathname;

    // 1. GET /api/ledger/sync
    if (req.method === 'GET' && pathname === '/api/ledger/sync') {
      let blocks = [];
      let blobs = [];
      let tokens = [];
      let audits = [];

      if (DatabaseSync && fs.existsSync(DB_PATH)) {
        try {
          const db = new DatabaseSync(DB_PATH, { readOnly: true });
          blocks = db.prepare('SELECT block_index, block_hash, prev_hash, merkle_root, timestamp, authority_name, tx_count FROM blocks ORDER BY block_index ASC').all();
          blobs = db.prepare('SELECT blob_id, previous_blob_id, owner_address, updater_address, timestamp FROM blobs').all();
          tokens = db.prepare('SELECT token_id, target_blob_id, grantor_address, recipient_address, valid_from, valid_until, parent_token_id, status, block_index FROM temporal_tokens').all();
          audits = db.prepare('SELECT id, token_id, blob_id, accessor_address, access_timestamp, block_index FROM decryption_audits').all();
        } catch (err) {
          console.warn('SQLite ledger read error:', err.message);
        }
      }

      const entities = loadRegisteredEntities();
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ blocks, blobs, tokens, audits, entities }));
      return;
    }

    // 2. GET /api/external/entities
    if (req.method === 'GET' && pathname === '/api/external/entities') {
      const entities = loadRegisteredEntities();
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(entities));
      return;
    }

    // 3. POST /api/external/entity
    if (req.method === 'POST' && (pathname === '/api/external/entity' || pathname === '/api/v1/external/entity')) {
      let bodyData = '';
      req.on('data', chunk => bodyData += chunk);
      req.on('end', async () => {
        try {
          const payload = JSON.parse(bodyData || '{}');
          const result = await processExternalEntity(payload);
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify(result));
        } catch (err) {
          res.writeHead(500, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: err.message }));
        }
      });
      return;
    }

    let reqPath = decodeURI(pathname);
    if (reqPath === '/' || reqPath === '') {
      reqPath = '/index.html';
    }

    const filePath = path.join(DIST_DIR, reqPath);

    if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
      const ext = path.extname(filePath).toLowerCase();
      const contentType = MIME_TYPES[ext] || 'application/octet-stream';
      res.writeHead(200, { 'Content-Type': contentType });
      fs.createReadStream(filePath).pipe(res);
    } else {
      res.writeHead(404, { 'Content-Type': 'text/plain' });
      res.end(`404 Not Found: ${reqPath}`);
    }
  });

  server.listen(port, () => {
    console.log(`====================================================`);
    console.log(`  Sanjeev Health Portal Online at: http://localhost:${port}`);
    console.log(`  Serving from: ${DIST_DIR}`);
    console.log(`====================================================`);
  });
}

function test() {
  console.log('[Test] Validating Sanjeev application integrity...');
  const files = [
    'public/index.html',
    'public/manifest.json',
    'public/favicon.svg',
    'src/index.js',
    'src/styles/app.css',
    'src/services/api.js',
    'src/services/crypto.js',
    'src/services/state.js',
    'src/services/openrouter.js',
    'src/services/native-keypair.js',
    'src/services/supabase.js',
    'src/config/node-config.js',
    'src/views/patient.js',
    'src/views/hospital.js',
    'src/views/explorer.js',
    'src/views/settings-modal.js',
    'src/views/auth-modal.js',
    'supabase_schema.sql'
  ];

  let passed = true;
  for (const f of files) {
    const fullPath = path.join(ROOT_DIR, f);
    if (!fs.existsSync(fullPath)) {
      console.error(`  [FAIL] Missing file: ${f}`);
      passed = false;
    } else {
      const stat = fs.statSync(fullPath);
      console.log(`  [PASS] ${f} (${stat.size} bytes)`);
    }
  }

  if (passed) {
    console.log('[Test] All application components verified successfully!');
  } else {
    process.exit(1);
  }
}

// CLI Argument Handling
const args = process.argv.slice(2);
if (args.includes('--serve') || args.includes('-s')) {
  const portIdx = args.findIndex(a => a === '--serve' || a === '-s');
  const port = parseInt(args[portIdx + 1], 10) || 3000;
  serve(port);
} else if (args.includes('--test')) {
  test();
} else {
  build();
}
