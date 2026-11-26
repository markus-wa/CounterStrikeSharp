# 09: Production keys, environment and domain

Spec: `.scratch/hardening-c-fast-fix/spec.md` (Trust and keys, Hosting and publishing). ADR-0006.

**What to build:** Make the feed real.

- Generate the CI signing key and store it as a secret in the data repo's protected environment, with required reviewers.
- Generate the offline backup key, age-encrypt it and store it on two offline media.
- Compile both public keys into the core, replacing the test key.
- Point the publish workflow at the host chosen in 02 and publish the first signed index.
- Set the default `GamedataFeedUrl` and the release `seq` floor.

**Blocked by:** 02, 08

**Status:** ready-for-human (key generation and custody, DNS, GitHub environment settings); compiling the keys and the default URL into the core can be done by an agent afterwards

**Tier:** 3 · must

- [ ] The private CI key exists only in the protected environment; the backup key exists only offline (recorded who holds it, not where)
- [ ] The first production index is live; `curl http://<domain>/...` returns it without a redirect
- [ ] A release build boots on the laptop against the production feed and reports an exact-build or carried-forward snapshot with the production key id
- [ ] Key rotation runbook (ship a release with the new key list) written into the data repo README
