# Firmware and artifact hashes

## Supported device firmware

```text
Model:        AAP-AN00
Device:       HNAAP
Display:      AAP-AN00 10.0.0.175(C00E170R104P3)
Incremental:  10DLDLD175C00E170
Fingerprint:  HONOR/AAP-AN00/HNAAP:16/HONORAAP-AN00/10DLDLD175C00E170:user/release-keys
Kernel:       6.12.38-android16-5-gfde7767f6ef6-abogki481467632-4k
```

## Exact kernel artifacts

These hashes identify the research input. The images are not redistributed in this repository.

```text
09447d275e3f66d9bd70f68893757464e416d82dc4cbb16c544a3a7b9023cfd0  Image_175
685ebbe298fb47321c6db6336adc2cb74da0b8ecca088242626544b2a3b0813d  boot_a_175.img
```

`Image_175` is the uncompressed kernel image used for call-site generation. `boot_a_175.img` identifies the boot image from which the research target was validated.

## Successful payload

```text
d97f486356914a10f3b7059fea9c39ed85e53fb295098da85aa414003d32b741  preload_honor_vote5.so
```

Verify on macOS:

```bash
cd prebuilt
shasum -a 256 -c SHA256SUMS.txt
```

Verify on Linux:

```bash
cd prebuilt
sha256sum -c SHA256SUMS.txt
```

## Rebuild inputs

An exact-firmware rebuild needs only the included source, generated headers, embedded su daemon, and an Android NDK. The kernel images are required only for new target research or table regeneration.
