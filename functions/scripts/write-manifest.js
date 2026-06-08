const { spawnSync } = require('node:child_process');
const path = require('node:path');

const functionsDir = path.resolve(__dirname, '..');
const firebaseFunctionsBin = path.join(
  functionsDir,
  'node_modules',
  'firebase-functions',
  'lib',
  'bin',
  'firebase-functions.js',
);

const result = spawnSync(process.execPath, [firebaseFunctionsBin, '.'], {
  cwd: functionsDir,
  env: {
    ...process.env,
    FUNCTIONS_MANIFEST_OUTPUT_PATH: 'functions.yaml',
  },
  stdio: 'inherit',
});

if (result.error) {
  console.error(result.error);
  process.exit(1);
}

process.exit(result.status ?? 1);
