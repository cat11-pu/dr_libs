# FLAC 解码提速验证

所有改动都在 `../dr_flac.h`，本目录仅存放测试素材、基准程序和报告，不参与库编译。

## 三类优化
1. 查表（LUT）：Rice 残差符号还原改用 `.rodata` 中的 2 项常量符号表（`signMaskTable` /
   `static const t[2]`），去掉每样本分支与每次调用的栈初始化，不产生堆分配。
2. 循环展开：零阶 Rice 解码主循环由逐样本改为 8 路展开，批量读取比特流、保持写入顺序，
   尾样本走原逐样本路径。LPC 标量/SSE4.1/NEON 路径统一复用静态符号表。
3. 按块并行：新增 `drflac_decode_memory_pcm_frames_s32_parallel()`（编译期定义
   `DRFLAC_USE_PTHREAD` 并链接 `-lpthread`）。FLAC 帧相互独立，按 PCM 帧区间均分给多个
   轻量解码器，各自 seek 到区间起点后直接写入同一输出缓冲的不相交切片，不复制整段输出。

## 构建与运行
```sh
# 串行（默认，不依赖 pthread）
gcc -O3 -march=native -o bench_final bench_final.c -lm
# 并行
gcc -O3 -march=native -DDRFLAC_USE_PTHREAD -o bench_final bench_final.c -lm -lpthread

./bench_final data/real_l8.flac 0 20        # 0 = 串行基线
./bench_final data/real_l8.flac 8 20        # 8 = 8 线程
```

## 正确性
- `verify_corpus`：修改版与原版头文件在 21 个真实 FLAC（8/16/24-bit、单/立体声）上逐样本一致。
- `par_corpus`：同一文件串行与 2/4/6/8 线程输出逐样本一致，含 STREAMINFO 长度虚报的 ID3 语料。
- 三种码率素材各 23,040,000 个样本，串行与各线程数输出 `cmp` 完全相同。

## 内存
并行只额外分配每线程的小型解码器结构与工作项/线程句柄；92MB 输出缓冲与输入镜像共享。
实测峰值 RSS 相对串行仅增加约 0.2%~1.0%。
