#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class ProjectionExecutor : public AbstractExecutor {
   private:
    std::unique_ptr<AbstractExecutor> prev_;        // 投影节点的儿子节点
    std::vector<ColMeta> cols_;                     // 需要投影的字段
    size_t len_;                                    // 字段总长度
    std::vector<size_t> sel_idxs_;                  

   public:
    ProjectionExecutor(std::unique_ptr<AbstractExecutor> prev, const std::vector<TabCol> &sel_cols) {
        prev_ = std::move(prev);

        size_t curr_offset = 0;
        auto &prev_cols = prev_->cols();
        for (auto &sel_col : sel_cols) {
            auto pos = get_col(prev_cols, sel_col);
            sel_idxs_.push_back(pos - prev_cols.begin());
            auto col = *pos;
            col.offset = curr_offset;
            curr_offset += col.len;
            cols_.push_back(col);
        }
        len_ = curr_offset;
    }

    void beginTuple() override {
        prev_->beginTuple();
    }

    void nextTuple() override {
        prev_->nextTuple();
    }


    std::unique_ptr<RmRecord> Next() override {
        //获取上一个算子（父算子）的列信息
        auto &prev_cols = prev_->cols();
        // 获取上一个算子（父算子）的下一个记录
        auto prev_rec = prev_->Next();
        // 获取投影算子需要投影的列信息
        auto &proj_cols = cols_;
        //创建一个用于存储投影结果的记录
        auto proj_rec = std::make_unique<RmRecord>(len_);
        //遍历投影的列
        for (size_t proj_idx = 0; proj_idx < proj_cols.size(); proj_idx++) {\
            //获取在上一个算子（父算子）中对应的列索引
            size_t prev_idx = sel_idxs_[proj_idx];
            //获取上一个算子（父算子）中对应列的元信息
            auto &prev_col = prev_cols[prev_idx];
            //获取当前投影算子中对应列的元信息
            auto &proj_col = proj_cols[proj_idx];
            // 利用memcpy生成proj_rec, 将上一个算子中对应列的值拷贝到当前投影记录中
            memcpy( proj_rec->data+proj_col.offset, prev_rec->data+prev_col.offset, prev_col.len );
        }
        return proj_rec;
    }


    Rid &rid() override { return _abstract_rid; }
};