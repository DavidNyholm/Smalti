# Where these fonts come from

smalti embeds Inter as its default font. These are unmodified copies of three static weights from the official Inter 4.1 release.

- Release: https://github.com/rsms/inter/releases/tag/v4.1
- Archive: `Inter-4.1.zip`, SHA-256 `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e`
- Files, taken from `extras/ttf/` in that archive:

| File | SHA-256 |
|---|---|
| `Inter-Regular.ttf` | `40d692fce188e4471e2b3cba937be967878f631ad3ebbbdcd587687c7ebe0c82` |
| `Inter-Medium.ttf` | `97ad806f526e41546d46365bb3a393145f75b7b1568913db74549ad8b8dba872` |
| `Inter-SemiBold.ttf` | `78a843fade9d4612a5567302fb595b56976eb5fcebf4fea5a5912d638bafcde3` |
| `OFL.txt` (the archive's `LICENSE.txt`) | `262481e844521b326f5ecd053e59b98c8b2da78c8ee1bdbb6e8174305e54935a` |

Inter is licensed under the SIL Open Font License 1.1 (`OFL.txt`), not under smalti's MIT licence. The OFL allows bundling the fonts inside software. Inter declares no Reserved Font Name.

To check the copies yourself, download the archive, compare its SHA-256 with the one above, and compare the extracted files with these.
