#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class NestedLoopJoinExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> left_;    // 左儿子节点（需要join的表）
    std::unique_ptr<AbstractExecutor> right_;   // 右儿子节点（需要join的表）
    size_t len_;                                // join后获得的每条记录的长度
    std::vector<ColMeta> cols_;                 // join后获得的记录的字段

    std::vector<Condition> fed_conds_;          // join条件
    bool isend;

   public:
    NestedLoopJoinExecutor(std::unique_ptr<AbstractExecutor> left, std::unique_ptr<AbstractExecutor> right, 
                            std::vector<Condition> conds) {
        left_ = std::move(left);
        right_ = std::move(right);
        len_ = left_->tupleLen() + right_->tupleLen();
        cols_ = left_->cols();
        auto right_cols = right_->cols();
        for (auto &col : right_cols) {
            col.offset += left_->tupleLen();
        }

        cols_.insert(cols_.end(), right_cols.begin(), right_cols.end());
        isend = false;
        fed_conds_ = std::move(conds);

    }

    void beginTuple() override {
        left_->beginTuple();
        if (left_->is_end()) {
            return;
        }
        feed_right();
        right_->beginTuple();
        while (right_->is_end()) {
            // 如果当前innerTable(右表或算子)扫描完了,就移动到outerTable(左表)下一个记录,然后把右表移动到第一个记录的位置
            left_->nextTuple();
            if (left_->is_end()) {
                break;
            }
            feed_right();
            right_->beginTuple();
    }

    void nextTuple() override {
        
    }

    std::unique_ptr<RmRecord> Next() override {
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};