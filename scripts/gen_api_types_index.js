const fs = require('fs');
const path = require('path');

const dir = path.join(__dirname, '..', 'docs', 'API_TYPES');
if (!fs.existsSync(dir)) {
  console.error('docs/API_TYPES directory does not exist');
  process.exit(1);
}

const files = fs.readdirSync(dir).filter(f => f.endsWith('.ts') && f !== 'index.ts');
const lines = files.map(f => `export * from './${f.replace(/\\.ts$/,'')}'`);
fs.writeFileSync(path.join(dir, 'index.ts'), lines.join('\n'), 'utf8');
console.log('Wrote docs/API_TYPES/index.ts');