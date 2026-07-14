# npm 0.1.0 发布记录

发布日期：2026-07-14  
版本：`0.1.0`  
发布 commit：`d019c2c63cf14acebfe48a5d27081cae7ed1e18e`  
协议：Apache-2.0

## 发布结果

`@arcships/light-ocr@0.1.0` 与它的模型包、四个平台原生包已经公开发布到 npm。六个包的 `next` 与 `latest` dist-tag 均指向 `0.1.0`。

推荐安装入口：

```bash
npm install @arcships/light-ocr
```

主包使用 exact-version 依赖安装 PP-OCRv6 Small 模型，并由 npm 的平台筛选安装当前主机所需的一个原生包。安装和运行没有 `install`/`postinstall` 下载或源码编译步骤。

## 自动化证据

- [Core CI run 29312484043](https://github.com/arcships/light-ocr/actions/runs/29312484043)：Linux x64 glibc、Windows x64、macOS arm64/x64、oracle 与 safety 六个 jobs 全部成功。
- [npm release run 29312486301](https://github.com/arcships/light-ocr/actions/runs/29312486301)：四平台构建、确定性双重 `npm pack`、Node.js 22/24 八组 package smoke、CJS/ESM、TypeScript、真实 PP-OCRv6、registry 分阶段发布、禁网运行与 `latest` 提升全部成功。
- npm provenance：六个包均由 GitHub Actions 使用 npm provenance 发布。
- 独立安装复验：macOS arm64、Node.js `22.13.0` 从公共 registry 安装主包后，不传 `bundlePath` 成功识别锁定 fixture `HELLO 123`。

## 不可变制品

以下数值来自成功 workflow 保存的 `release-manifest.json`；npm registry 的 `dist.integrity` 已逐包复核一致。

| Package | Tarball bytes | Unpacked bytes | SHA-256 |
| --- | ---: | ---: | --- |
| `@arcships/light-ocr` | 8,512 | 26,482 | `1667a38273d5a6074d8dcf98acd9dbeff7cad2b753ee7fdd5f60042a4ff67ea5` |
| `@arcships/light-ocr-model-ppocrv6-small` | 26,091,093 | 31,332,789 | `95b04d965b174f84b34fde331c595b834982977fa40ba2e0957f96b9a6ca17e3` |
| `@arcships/light-ocr-darwin-arm64` | 11,909,885 | 39,660,798 | `2f3a855144db668347b77092b1c4716841c69cdacbbaaee8078de16645daa869` |
| `@arcships/light-ocr-darwin-x64` | 13,833,430 | 45,653,816 | `63e4eaa715726b00709af438309bb7f5bfa70b8d7f92746a0b6ff80a2bada841` |
| `@arcships/light-ocr-linux-x64-gnu` | 11,692,190 | 32,122,414 | `a88fe176ce19833cf614b8568a855322537ff73feffb70399d88a71e6743e9ee` |
| `@arcships/light-ocr-win32-x64` | 6,159,009 | 16,391,422 | `20f9b1d24d63459a5807ccfb420b8263ad83f2d4f11bba55634428b4b2056e30` |

Registry integrity：

```text
@arcships/light-ocr
sha512-54OUabOUvO2BYDaU5mLsM2f5cYWkvY258MzdOq+9lJHKXo4Aosj6zWyLhTCHZX00Vjz6Ky3mfv0K0TExZU+BaA==

@arcships/light-ocr-model-ppocrv6-small
sha512-xp4h3P924QtS7RSrx36djTag+vTKmPUh6vL8lEeZar+zjR3rnwKWvQytAEqJo9Y6Z2i5b9K1yb4AcQLbWNb9ow==

@arcships/light-ocr-darwin-arm64
sha512-JUMGDJtviyFdYMErMlNu+vJDWFKQdskFdunnoRx5N0wvu9ZK/1vC99aPAX5dJxNiVnVB4X/HuEMwS+niID4H0A==

@arcships/light-ocr-darwin-x64
sha512-vFQdFPwMXr+KLO+OSOFZBzCDoLtGT8GVBxQmQHkRhcKuE/eZjUwi9cmLgv3ZCHMWWZcv1suzddqmFc5C2fazJA==

@arcships/light-ocr-linux-x64-gnu
sha512-pC9UcqoCbS7q8tMR4Zfn3omonWksPLNgwMTQrXMQdNb4leAMaZ6IXapShR7LeocPQbOkWD8C4Q6+MjW7KvtCJg==

@arcships/light-ocr-win32-x64
sha512-vJghMn0FlBJdXvHd5q9RdpXdrYhAhA2p3Dto5rds8oCkqQGyt6mdC/4DNeMtvY/BDGIPB39GEScei+hEf/pkxw==
```

模型 package 中 bundle payload 的身份仍由 `ppocrv6-small-onnx-20260714.1`、manifest、`SHA256SUMS` 与 Core directory loader 共同验证；npm tarball integrity 不能替代安装后模型内容校验。
