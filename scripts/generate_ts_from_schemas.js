#!/usr/bin/env node
// Generate simple TypeScript interfaces from JSON Schema files under api_schema/
// This is intentionally dependency-free to avoid pulling deprecated npm packages.
//
// Usage: node scripts/generate_ts_from_schemas.js
//
// Output: docs/API_TYPES/<schema-basename>.ts and docs/API_TYPES/index.ts

const fs = require('fs');
const path = require('path');

const SCHEMA_ROOT = path.join(__dirname, '..', 'api_schema');
const OUT_DIR = path.join(__dirname, '..', 'docs', 'API_TYPES');

function walk(dir) {
  let results = [];
  const list = fs.readdirSync(dir);
  list.forEach((file) => {
    const p = path.join(dir, file);
    const stat = fs.statSync(p);
    if (stat && stat.isDirectory()) {
      results = results.concat(walk(p));
    } else if (file.endsWith('.schema.json')) {
      results.push(p);
    }
  });
  return results;
}

function toPascalCase(s) {
  return s
    .split(/[^a-zA-Z0-9]+/)
    .filter(Boolean)
    .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
    .join('');
}

function typeFromSchema(schema) {
  if (!schema) return 'any';
  if (schema.$ref) return 'any';
  if (schema.enum) {
    return schema.enum.map((v) => JSON.stringify(v)).join(' | ');
  }
  const t = schema.type;
  if (Array.isArray(t)) {
    // pick first simple mapping or union
    const mapped = t.map((tp) => mapSimpleType(tp, schema));
    return mapped.join(' | ');
  }
  return mapSimpleType(t, schema);
}

function mapSimpleType(t, schema) {
  switch (t) {
    case 'string':
      return 'string';
    case 'integer':
    case 'number':
      return 'number';
    case 'boolean':
      return 'boolean';
    case 'array': {
      const items = schema.items || {};
      const itemType = typeFromSchema(items) || 'any';
      return `${itemType}[]`;
    }
    case 'object': {
      if (schema.properties) {
        const props = schema.properties;
        const required = new Set(schema.required || []);
        const inner = Object.keys(props)
          .map((k) => {
            const propSchema = props[k];
            const propType = typeFromSchema(propSchema);
            const opt = required.has(k) ? '' : '?';
            return `  ${k}${opt}: ${propType};`;
          })
          .join('\n');
        return `{\n${inner}\n}`;
      }
      if (schema.additionalProperties) {
        return 'Record<string, any>';
      }
      return 'Record<string, any>';
    }
    default:
      // fallback: handle oneOf/anyOf/oneOf
      if (schema.oneOf) {
        return schema.oneOf.map(typeFromSchema).join(' | ') || 'any';
      }
      if (schema.anyOf) {
        return schema.anyOf.map(typeFromSchema).join(' | ') || 'any';
      }
      return 'any';
  }
}

function generateInterface(schemaPath) {
  const raw = fs.readFileSync(schemaPath, 'utf8');
  const schema = JSON.parse(raw);
  const base = path.basename(schemaPath).replace('.schema.json', ''); // e.g., lean.exec
  const outName = base + '.ts';
  const interfaceName = (schema.title && schema.title.replace(/[^a-zA-Z0-9]/g, '')) || (toPascalCase(base) + 'Params');

  const required = new Set(schema.required || []);
  const props = schema.properties || {};

  const lines = [];
  lines.push(`/**`);
  lines.push(` * Auto-generated from ${path.relative(process.cwd(), schemaPath)}`);
  lines.push(` * Schema title: ${schema.title || ''}`);
  lines.push(` */`);
  lines.push('');
  lines.push(`export interface ${interfaceName} {`);

  for (const key of Object.keys(props)) {
    const propSchema = props[key];
    const tsType = typeFromSchema(propSchema);
    const optional = required.has(key) ? '' : '?';
    lines.push(`  ${key}${optional}: ${tsType};`);
  }

  // If there are no properties, emit an index signature to allow flexible objects
  if (Object.keys(props).length === 0) {
    lines.push(`  [key: string]: any;`);
  }

  lines.push('}');
  lines.push('');

  return { outName, content: lines.join('\n') };
}

function ensureOutDir() {
  if (!fs.existsSync(OUT_DIR)) {
    fs.mkdirSync(OUT_DIR, { recursive: true });
  }
}

function writeIndex() {
  const files = fs.readdirSync(OUT_DIR).filter((f) => f.endsWith('.ts') && f !== 'index.ts');
  const lines = files.map((f) => `export * from './${f.replace(/\\.ts$/, '')}';`);
  fs.writeFileSync(path.join(OUT_DIR, 'index.ts'), lines.join('\n') + '\n', 'utf8');
  console.log('Wrote docs/API_TYPES/index.ts');
}

function main() {
  const files = walk(SCHEMA_ROOT);
  if (files.length === 0) {
    console.error('No schema files found under', SCHEMA_ROOT);
    process.exit(1);
  }
  ensureOutDir();
  for (const f of files) {
    try {
      const { outName, content } = generateInterface(f);
      const outPath = path.join(OUT_DIR, outName);
      fs.writeFileSync(outPath, content, 'utf8');
      console.log('Wrote', outPath);
    } catch (err) {
      console.error('Failed generating for', f, err);
    }
  }
  writeIndex();
}

main();