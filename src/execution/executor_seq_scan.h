#pragma once

#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class SeqScanExecutor : public AbstractExecutor {
   private:
    std::string tab_name_;              // 表的名称
    std::vector<Condition> conds_;      // scan的条件
    RmFileHandle *fh_;                  // 表的数据文件句柄
    std::vector<ColMeta> cols_;         // scan后生成的记录的字段
    size_t len_;                        // scan后生成的每条记录的长度
    std::vector<Condition> fed_conds_;  // 同conds_，两个字段相同

    Rid rid_;
    std::unique_ptr<RecScan> scan_;     // table_iterator

    SmManager *sm_manager_;

   public:
    SeqScanExecutor(SmManager *sm_manager, std::string tab_name, std::vector<Condition> conds, Context *context) {
        sm_manager_ = sm_manager;
        tab_name_ = std::move(tab_name);
        conds_ = std::move(conds);
        TabMeta &tab = sm_manager_->db_.get_table(tab_name_);
        fh_ = sm_manager_->fhs_.at(tab_name_).get();
        cols_ = tab.cols;
        len_ = cols_.back().offset + cols_.back().len;

        context_ = context;

        fed_conds_ = conds_;
    }

    void beginTuple() override {
        // 创建一个记录扫描器，用于扫描文件记录
        scan_ = std::make_unique<RmScan>(fh_);
        // 得到第一个满足fed_conds_条件的record,并把其rid赋给算子成员rid_
        while (!scan_->is_end()) { 
            // 获取扫描器当前记录的 rid
            rid_ = scan_->rid();
            try {
                auto rec = fh_->get_record(rid_, context_); 
                // 利用eval_conds判断是否当前记录(rec.get())满足谓词条件、满足则中止循环
                if( eval_conds( cols_, fed_conds_, rec.get() ) ) 
                    break;
            } 
            // 捕获记录未找到的异常
            catch (RecordNotFoundError &e) {
                std::cerr << e.what() << std::endl;
            }
            // 找下一个有record的位置
            scan_->next();  
        }
    }

    void nextTuple() override {
        assert(!is_end());
        // 用table_iterator遍历TableHeap中的所有Tuple
        for (scan_->next(); !scan_->is_end(); scan_->next()) {  
            // 获取当前记录赋给算子成员rid_
            rid_ = scan_->rid();
            try {
                auto rec = fh_->get_record(rid_, context_);  
                // 利用eval_conds判断是否当前记录(rec.get())满足谓词条件
                if( eval_conds( cols_, fed_conds_, rec.get() ) )
                    break;
            }
            // 捕获记录未找到的异常 
            catch (RecordNotFoundError &e) {
                std::cerr << e.what() << std::endl;
            }
        }
    }

    std::unique_ptr<RmRecord> Next() override {
        // 利用fh_得到记录record
        return fh_->get_record( rid_, context_ );
    }

    Rid &rid() override { return rid_; }

    bool eval_cond(const std::vector<ColMeta> &rec_cols, const Condition &cond, const RmRecord *rec) {
        // 获取左操作数列的元数据
        auto lhs_col = get_col(rec_cols, cond.lhs_col);
        // 获取左操作数的指针
        char *lhs = rec->data + lhs_col->offset;
        // 右操作数的指针和类型
        char *rhs;
        ColType rhs_type;

        // 如果右操作数是值
        if (cond.is_rhs_val) {
            // 获取右操作数的类型和指针
            rhs_type = cond.rhs_val.type;
            rhs = cond.rhs_val.raw->data;
        } else {
            // 如果右操作数是一个列
            // 获取右操作数列的元数据
            auto rhs_col = get_col(rec_cols, cond.rhs_col);
            // 获取右操作数的类型和指针
            rhs_type = rhs_col->type;
            rhs = rec->data + rhs_col->offset;
        }

        // 断言左右操作数类型相同
        assert(rhs_type == lhs_col->type);  
        // 进行比较
        int cmp = ix_compare(lhs, rhs, rhs_type, lhs_col->len);

        // 根据条件运算符进行比较，返回比较结果
        if (cond.op == OP_EQ) {
            return cmp == 0;
        } else if (cond.op == OP_NE) {
            return cmp != 0;
        } else if (cond.op == OP_LT) {
            return cmp < 0;
        } else if (cond.op == OP_GT) {
            return cmp > 0;
        } else if (cond.op == OP_LE) {
            return cmp <= 0;
        } else if (cond.op == OP_GE) {
            return cmp >= 0;
        } 
        else {
            // 如果条件运算符不是已知的类型，抛出异常
            throw InternalError("Unexpected op type");
            }
    }

    //用于检查给定记录是否满足所有给定的条件
    bool eval_conds(const std::vector<ColMeta> &rec_cols, const std::vector<Condition> &conds, const RmRecord *rec) {
        return std::all_of(conds.begin(), conds.end(),
                           [&](const Condition &cond) { return eval_cond(rec_cols, cond, rec); });
    }
};