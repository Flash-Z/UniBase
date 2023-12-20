#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class UpdateExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;
    std::vector<Condition> conds_;
    RmFileHandle *fh_;
    std::vector<Rid> rids_;
    std::string tab_name_;
    std::vector<SetClause> set_clauses_;
    SmManager *sm_manager_;

   public:
    UpdateExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<SetClause> set_clauses,
                   std::vector<Condition> conds, std::vector<Rid> rids, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = tab_name;
        set_clauses_ = set_clauses;
        tab_ = sm_manager_->db_.get_table(tab_name);
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        conds_ = conds;
        rids_ = rids;
        context_ = context;
    }
    std::unique_ptr<RmRecord> Next() override {
        // 初始化索引处理程序的指针向量，初始值为nullptr
        std::vector<IxIndexHandle *> ihs(tab_.cols.size(), nullptr);
        // 遍历设置子句集合，为设置子句中的列设置对应的索引处理程序指针
        for (auto &set_clause : set_clauses_) {
            // 获取设置子句左侧列的元数据
            auto lhs_col = tab_.get_col(set_clause.lhs.col_name);
            // 检查左侧列是否具有索引，若存在索引，则准备索引处理程序指针
            if (lhs_col->index) {
                // 获取左侧列的索引位置
                size_t lhs_col_idx = lhs_col - tab_.cols.begin();
                // 从索引管理器获取指定索引的名称，并获取索引处理程序，存储在 ihs 向量对应位置
                ihs[lhs_col_idx] = sm_manager_->ihs_.at( sm_manager_->get_ix_manager()->get_index_name(tab_name_,lhs_col_idx) ).get();
            }
        }
        // 遍历记录标识符集合，执行更新操作
        for (auto &rid : rids_) {
            // 获取记录
            auto rec = fh_->get_record(rid, context_);
            // 遍历设置子句集合
            for( size_t i = 0; i < set_clauses_.size(); i++ ) {
                // 获取设置子句左侧列的元数据
                auto col = tab_.get_col(set_clauses_[i].lhs.col_name);
                // 将设置子句的右侧值复制到记录中指定列的位置
                memcpy(rec->data+col->offset,set_clauses_[i].rhs.raw->data,col->len);
                // 检查列是否具有索引，如果有，删除之前存储的值
                if( col->index )
                     ihs[i]->delete_entry( rec->data+col->offset, context_->txn_ );
            }
        }
        return nullptr;
    }

    Rid &rid() override { return _abstract_rid; }
};