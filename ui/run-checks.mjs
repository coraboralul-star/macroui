import { readFileSync, writeFileSync } from "node:fs";
import { createServer } from "vite";

// Both engines play this fixture too. --write-fixture rewrites it from the UI.
const fixturePath = new URL("../engine-native/tests/fixtures/create-paths.json", import.meta.url);

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
  }
  const failures = mod.runBugChecks(JSON.parse(readFileSync(fixturePath, "utf8")));
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
