# 设置备份 / Settings backups

交换文件保持v25。完整配对文件使用v29(PICOSETT)，阅读资料前是12字节有标记扩展：KRE、布局号1、低残影0/1、封面0/1、上下边距0..48、保留字节0及前8字节的FNV-1a校验。恢复先完整校验，逐键保留回退日志并暂存进度，全部提交后公开；失败/断电保留恢复材料，不清命名空间或备份外记录。

Interchange stays v25. Full paired backups use v29(PICOSETT): before history, a12-byte block contains KRE,layout1,low-ghost0/1,covers0/1,vertical margin0..48,reserved0 and FNV-1a over the first8bytes. Validate before per-key checkpointing and staged progress; publish after complete commit. Failures/interruption retain rollback material without clearing namespaces or unrelated records.

读取v1..27及已交付本地v28低残影备份。两字节v28边距草案可识别；未发布的封面草案与低残影草案都用了完全相同的五字节v28布局，无法由字节推断来源。五字节v28沿用已交付低残影语义，不据此改变封面开关；这类封面草案须先由原作者版本恢复并导出明确格式。缺少新字段的旧备份保留当前低残影和封面选择。

Read v1..27 and delivered local v28 low-ghost backups. The two-byte v28 margin draft is distinguishable,while the unpublished cover draft reused identical five-byte bytes with different semantics. Interpret that layout as delivered low-ghost data without inferring a cover choice; restore an ambiguous cover draft using its original writer before exporting a tagged version. Legacy streams lacking new fields retain current low-ghost and cover preferences.
