import { readFileSync, writeFileSync } from "node:fs";
import { createServer } from "vite";

// Both engines play these fixtures too. --write-fixture rewrites them from the UI.
const fixturePath = new URL("../engine-native/tests/fixtures/create-paths.json", import.meta.url);
const radialPath = new URL("../engine-native/tests/fixtures/radial.json", import.meta.url);

const server = await createServer({
  server: { middlewareMode: true },
  appType: "custom",
  logLevel: "error",
});

try {
  const mod = await server.ssrLoadModule("/src/bugchecks.ts");
  if (process.argv.includes("--write-fixture")) {
    writeFileSync(fixturePath, `${JSON.stringify(mod.createPathsFixture(), null, 2)}\n`);
    console.log(`wrote ${fixturePath.pathname}`);
    writeFileSync(radialPath, `${JSON.stringify(mod.radialFixture(), null, 2)}\n`);
    console.log(`wrote ${radialPath.pathname}`);
  }
  const failures = mod.runBugChecks(JSON.parse(readFileSync(fixturePath, "utf8")), JSON.parse(readFileSync(radialPath, "utf8")));
  if (failures.length) {
    for (const failure of failures) console.error(`FAIL ${failure}`);
    console.error(`FAIL ${failures.length}`);
    process.exitCode = 1;
  } else {
    console.log("PASS");
  }
} finally {
  await server.close();
}
