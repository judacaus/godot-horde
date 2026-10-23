// Loads a Godot web export in headless Chromium and reports console output plus
// window.hordeResult. Usage: node run.js <url> [timeout_ms]
const { chromium } = require('playwright-core');
(async () => {
	const url = process.argv[2];
	const timeout = parseInt(process.argv[3] || '90000', 10);
	const browser = await chromium.launch({
		executablePath: process.env.CHROMIUM || '/opt/pw-browsers/chromium-1243/chrome-linux64/chrome',
		args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
	});
	const page = await browser.newPage();
	const logs = [];
	page.on('console', (m) => logs.push(`[${m.type()}] ${m.text()}`));
	page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));
	await page.goto(url);
	const env = await page.evaluate(() => ({ crossOriginIsolated: window.crossOriginIsolated, isSecureContext: window.isSecureContext, sab: typeof SharedArrayBuffer !== 'undefined' }));
	let result = null;
	try {
		await page.waitForFunction(() => window.hordeResult !== undefined, null, { timeout });
		result = await page.evaluate(() => window.hordeResult);
	} catch (e) {
		result = 'TIMEOUT (no result reported)';
	}
	const status = await page.evaluate(() => { const n = document.getElementById('status-notice'); return n && n.offsetParent !== null ? n.innerText : ''; });
	if (process.argv[4]) { await page.screenshot({ path: process.argv[4] }); }
	console.log(JSON.stringify({ url, env, result, status_notice: status }, null, 1));
	console.log(logs.filter((l) => /HORDE|rror|xtension|GDExtension|Secure|Cross/.test(l)).slice(0, 25).join('\n'));
	await browser.close();
})();
