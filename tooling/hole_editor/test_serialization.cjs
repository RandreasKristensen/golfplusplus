// Exercise the editor's actual save functions without a browser or DOM.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const html = fs.readFileSync(path.join(__dirname, 'hole-editor.html'), 'utf8');
const context = vm.createContext({});
for (const name of ['r2', 'zoneShape', 'normalizeHole', 'buildJSON']) {
  const start = html.indexOf(`function ${name}(`);
  assert(start >= 0, `missing editor function ${name}`);
  const end = name === 'r2' ? html.indexOf('\n', start) : html.indexOf('\n}', start) + 2;
  assert(end > start, `missing end of editor function ${name}`);
  vm.runInContext(html.slice(start, end), context);
}

const original = {
  tee: [0, 0, 0], pin: [0, 0, 80],
  spline: { width: 18, rough_width: 24, control_points: [[0, 0, 0], [0, 0, 80]] },
  material_zones: [{
    type: 'green', outline: [[0, 70], [5, 80], [-5, 80]],
    green_stimp_feet: 9.5, source: { osm_id: 42 },
  }],
};
context.hole = structuredClone(original);
// An unrelated touch-up must not change the green's gameplay or provenance.
context.hole.tee[0] = 1.234;
const saved = JSON.parse(vm.runInContext('buildJSON()', context));
assert.equal(saved.tee[0], 1.23);
assert.deepEqual(saved.material_zones, original.material_zones);
context.hole = structuredClone(saved);
assert.deepEqual(JSON.parse(vm.runInContext('buildJSON()', context)), saved);
process.stdout.write('Hole editor serialization passed\n');
