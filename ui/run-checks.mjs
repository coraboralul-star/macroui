import { createServer } from "vite";

const server = await createServer({
  server: { middlewareMode: true },
  appType: "custom",
  logLevel: "error",
});

try {
  const mod = await server.ssrLoadModule("/src/bugchecks.ts");
  const failures = mod.runBugChecks();
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
