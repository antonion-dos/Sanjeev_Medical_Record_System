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

function serve(port = 3000) {
  build();

  const server = http.createServer((req, res) => {
    // Enable CORS
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, OPTIONS');

    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }

    let reqPath = decodeURI(req.url.split('?')[0]);
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
    'src/services/auth.js',
    'src/components/AuthModal.js',
    'src/styles/auth.css',
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
