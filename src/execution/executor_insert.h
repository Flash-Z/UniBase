#pragma once
#include "execution_defs.h"
#include "execution_manager.h"
#include "executor_abstract.h"
#include "index/ix.h"
#include "system/sm.h"

class InsertExecutor : public AbstractExecutor {
   private:
    TabMeta tab_;                   // 表的元数据
    std::vector<Value> values_;     // 需要插入的数据
    RmFileHandle *fh_;              // 表的数据文件句柄
    std::string tab_name_;          // 表名称
    Rid rid_;                       // 插入的位置，由于系统默认插入时不指定位置，因此当前rid_在插入后才赋值
    SmManager *sm_manager_;

   public:
    InsertExecutor(SmManager *sm_manager, const std::string &tab_name, std::vector<Value> values, Context *context) {
        sm_manager_ = sm_manager;
        tab_ = sm_manager_->db_.get_table(tab_name);
        values_ = values;
        tab_name_ = tab_name;
        if (values.size() != tab_.cols.size()) {
            throw InvalidValueCountError();
        }
        fh_ = sm_manager_->fhs_.at(tab_name).get();
        context_ = context;
    };

    std::unique_ptr<RmRecord> Next() override { 
        // 创建一个新的记录（rec），大小由文件头定义
        RmRecord rec(fh_->get_file_hdr().record_size);
        // 遍历待插入的值，将其设置到记录中对应的列中
        for (size_t i = 0; i < values_.size(); i++) {
            auto &col = tab_.cols[i];
            auto &val = values_[i];
            // 检查列类型是否匹配待插入值的类型
            if (col.type != val.type) {
            throw IncompatibleTypeError(coltype2str(col.type), coltype2str(val.type));
            }
            // 初始化值为原始数据格式
            val.init_raw(col.len);
            // 将值的原始数据复制到记录中对应的列中
            memcpy(rec.data + col.offset, val.raw->data, col.len);
        }
        // 插入到记录文件中，并获取记录标识符
        rid_ = fh_->insert_record(rec.data, context_);
        // 如果某些列存在索引，将记录插入到这些索引中
        for (size_t i = 0; i < tab_.cols.size(); i++) {
            auto &col = tab_.cols[i];
            // 检查列是否存在索引
            if (col.index) { // 存在索引，执行更新操作
                auto ih = sm_manager_->ihs_.at(sm_manager_->get_ix_manager()->get_index_name(tab_name_, i)).get();
                ih->insert_entry(rec.data + col.offset, rid_, context_->txn_);
            }
        }
        // 修改写入集，跟踪插入操作
        WriteRecord *wrec = new WriteRecord(WType::INSERT_TUPLE, tab_name_, rid_);
        context_->txn_->AppendWriteRecord(wrec);
        return nullptr;
    }
    Rid &rid() override { return rid_; }
};