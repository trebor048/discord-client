// Follow-up fixes for the 3 missed Chinese segments.
import { readFileSync, writeFileSync } from "node:fs";

const BASE = "C:\\Users\\me\\.dsh\\profiles\\desktop\\node_modules";

const JOBS = [
	{
		path: `${BASE}\\dsh-client-auto-continue\\lib\\client.js`,
		label: "dsh-client-auto-continue/lib/client.js",
		pairs: [
			["`扫描失败(${attempt + 1}/${attempts === Infinity ? \"∞\" : attempts}): ${error instanceof Error ? error.message : String(error)}`", "`scan failed (${attempt + 1}/${attempts === Infinity ? \"∞\" : attempts}): ${error instanceof Error ? error.message : String(error)}`", 1]
		]
	},
	{
		path: `${BASE}\\dsh-resume\\lib\\index.js`,
		label: "dsh-resume/lib/index.js",
		pairs: [
			["a visible \"继续\" user message", "a visible \"Continue\" user message", 1]
		]
	},
	{
		path: `${BASE}\\dsh-resume\\lib\\patch.js`,
		label: "dsh-resume/lib/patch.js",
		pairs: [
			["a later \"继续\" would make the model start thinking from", "a later \"Continue\" would make the model start thinking from", 1]
		]
	}
];

const CJK = /[\u4E00-\u9FFF\u3400-\u4DBF\uF900-\uFAFF\u3000-\u303F\uFF00-\uFFEF]/g;

let failed = false;
for (const job of JOBS) {
	let content = readFileSync(job.path, "utf8");
	if (content.charCodeAt(0) === 0xFEFF) content = content.slice(1);

	for (const [orig, , expected] of job.pairs) {
		const count = content.split(orig).length - 1;
		if (count !== expected) {
			console.error(`COUNT MISMATCH in ${job.label}: expected ${expected} got ${count} for: ${JSON.stringify(orig.slice(0, 90))}`);
			failed = true;
		}
	}
	if (failed) continue;

	let out = content;
	let total = 0;
	for (const [orig, repl] of job.pairs) {
		const parts = out.split(orig);
		total += parts.length - 1;
		out = parts.join(repl);
	}

	const remaining = (out.match(CJK) ?? []).length;
	writeFileSync(job.path, out, "utf8");
	console.log(`OK ${job.label}: ${total} replacements, remaining CJK chars: ${remaining}`);
}

if (failed) {
	console.error("Fix job failed — nothing written for failed jobs.");
	process.exit(1);
} else {
	console.log("FIXES DONE.");
}
