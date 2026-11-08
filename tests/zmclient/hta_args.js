// Runs tools/mod-launcher.hta's own refresh() and args() under node with a stubbed DOM, for tests/zmclient:
//   node hta_args.js <cases.json>      cases: [{map: <index in the .hta's MAPS>, ids: {<element id>: value or bool}}]
// prints {maps: the .hta's MAPS, lines: the command line args() gives for each case}.
var fs = require("fs");
var path = require("path");
var tools = path.join(__dirname, "..", "..", "tools");
var hta = fs.readFileSync(path.join(tools, "mod-launcher.hta"), "latin1");
global.hordeFlags = require(path.join(tools, "horde_flags.js"));
global.nightmareFlags = require(path.join(tools, "nightmare_flags.js"));

function grab(re) {
  var m = hta.match(re);
  if (!m) throw new Error("mod-launcher.hta changed: no match for " + re);
  return m[0];
}
var MAPS = eval("(" + grab(/\[\s*\["zombie_pentagon"[\s\S]*?\n\];/).replace(/;$/, "") + ")");
// the functions as the .hta defines them, evaluated in this scope so they see el(), MAPS, R and P
var source = grab(/function refresh\(\) \{[\s\S]*?\n\}/) + "\n" + grab(/function args\(\) \{[\s\S]*?\n\}/);

var cases = JSON.parse(fs.readFileSync(process.argv[2], "utf8"));
var out = [];
cases.forEach(function (c) {
  var els = {};
  function el(id) {
    if (!els[id]) els[id] = { checked: false, value: "", disabled: false, className: "", innerHTML: "", innerText: "" };
    return els[id];
  }
  Object.keys(c.ids).forEach(function (id) {
    var v = c.ids[id];
    if (typeof v === "boolean") el(id).checked = v; else el(id).value = v;
  });
  el("map").value = c.map;
  var R = "R", P = "P";
  eval(source);
  refresh();
  out.push(args());
});
process.stdout.write(JSON.stringify({ maps: MAPS, lines: out }));
