#ifndef AFFECT_OPTIONS_H
#define AFFECT_OPTIONS_H

/*
 * 决策层（affect）的模块 Options：由 Bootstrap/RuntimeSettings 从配置节 affect
 * 映射而来（Affect 不 include Configuration），与其他模块的 Options 一样放在全局命名空间。
 * enforce 实时生效：使用方持 const Live<AffectOptions> &，每次决策时取快照（ADR-023）；
 * enabled、policy、judge_model 仍按启动值（L2.c 接通）。
 */
#include <string>

struct AffectOptions
{
    bool enabled = false;
    std::string policy = "baseline"; // baseline / judge
    bool enforce = false;            // false = judge 只记录计划（影子）
    std::string judgeModel;          // 空 = 用 models.worker
};

#endif // AFFECT_OPTIONS_H
