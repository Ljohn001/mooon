// Writed by yijian (eyjian@qq.com, eyjian@gmail.com)
#include "config_loader.h"
#include <errno.h>
#include <fstream>
#include <mooon/net/utils.h>
#include <mooon/sys/file_utils.h>
#include <mooon/sys/mysql_db.h>
#include <mooon/sys/utils.h>
#include <mooon/utils/md5_helper.h>
#include <sys/inotify.h> // 一些低版本内核没有实现
namespace mooon { namespace db_proxy {

// 线程级DB连接
static __thread sys::DBConnection* g_db_connection[MAX_DB_CONNECTION] = { NULL } ;

static void init_db_info_array(struct DbInfo* db_info_array[])
{
    for (int i=0; i<MAX_DB_CONNECTION; ++i)
        db_info_array[i] = NULL;
}

static void init_query_info_array(struct QueryInfo* query_info_array[])
{
    for (int i=0; i<MAX_SQL_TEMPLATE; ++i)
        query_info_array[i] = NULL;
}

static void init_update_info_array(struct UpdateInfo* update_info_array[])
{
    for (int i=0; i<MAX_SQL_TEMPLATE; ++i)
        update_info_array[i] = NULL;
}

static void release_db_info_array(struct DbInfo* db_info_array[])
{
    for (int i=0; i<MAX_DB_CONNECTION; ++i)
    {
        delete db_info_array[i];
        db_info_array[i] = NULL;
    }
}

static void release_query_info_array(struct QueryInfo* query_info_array[])
{
    for (int i=0; i<MAX_SQL_TEMPLATE; ++i)
    {
        delete query_info_array[i];
        query_info_array[i] = NULL;
    }
}

static void release_update_info_array(struct UpdateInfo* update_info_array[])
{
    for (int i=0; i<MAX_SQL_TEMPLATE; ++i)
    {
        delete update_info_array[i];
        update_info_array[i] = NULL;
    }
}

////////////////////////////////////////////////////////////////////////////////
SINGLETON_IMPLEMENT(CConfigLoader);

std::string CConfigLoader::get_filepath()
{
    std::string program_path = sys::CUtils::get_program_path();
    std::string filepath = program_path + "/../conf/sql.json";

    if (access(filepath.c_str(), F_OK) != 0)
    {
        MYLOG_DETAIL("%s not exist\n", filepath.c_str());
        filepath = program_path + "/sql.json";
    }

    return filepath;
}

void CConfigLoader::monitor()
{
    while (true)
    {
        sys::CUtils::millisleep(2000);

        std::string filepath = CConfigLoader::get_filepath();
        (void)load(filepath);
    }
}

CConfigLoader::CConfigLoader()
{
    init_db_info_array(_db_info_array);
    init_query_info_array(_query_info_array);
    init_update_info_array(_update_info_array);

    // 无效md5值
    _md5_sum = "-";
}

bool CConfigLoader::load(const std::string& filepath)
{
    Json::Reader reader;
    Json::Value root;
    std::ifstream fs(filepath.c_str());
    struct DbInfo* db_info_array[MAX_DB_CONNECTION] = { NULL };
    struct QueryInfo* query_info_array[MAX_SQL_TEMPLATE] = { NULL };
    struct UpdateInfo* update_info_array[MAX_SQL_TEMPLATE] = { NULL };

    if (_md5_sum.empty())
    {
        MYLOG_INFO("loading %s\n", filepath.c_str());
    }
    else
    {
        MYLOG_DETAIL("loading %s\n", filepath.c_str());
    }
    if (!fs)
    {
        MYLOG_ERROR("load %s failed: %s\n", filepath.c_str(), strerror(errno));
        return false;
    }
    if (!reader.parse(fs, root))
    {
        MYLOG_ERROR("parse %s failed: %s\n", filepath.c_str(), reader.getFormattedErrorMessages().c_str());
        return false;
    }

    // 检查文件是否有修改过
    std::string md5_sum = utils::CMd5Helper::lowercase_md5("%s", root.toStyledString().c_str());
    if (md5_sum == _md5_sum)
    {
        MYLOG_DETAIL("not changed: (%s)%s\n", md5_sum.c_str(), filepath.c_str());
        return true; // 未发生变化
    }

    init_db_info_array(db_info_array);
    init_query_info_array(query_info_array);
    init_update_info_array(update_info_array);
    if (!load_database(root["database"], db_info_array))
        return false;
    if (!load_query(root["query"], query_info_array))
        return false;
    if (!load_update(root["update"], update_info_array))
        return false;

    int i; // 加写锁
    sys::WriteLockHelper write_lock(_read_write_lock);

    release_db_info_array(_db_info_array);
    release_query_info_array(_query_info_array);
    release_update_info_array(_update_info_array);
    for (i=0; i<MAX_DB_CONNECTION; ++i)
    {
        if (db_info_array[i] != NULL)
        {
            // 启动时即连接一下，以早期发现配置等问题
            _db_info_array[i] = new struct DbInfo(*db_info_array[i]);
            sys::DBConnection* db_connection = init_db_connection(i, false);
            if (db_connection != NULL)
            {
                delete db_connection;
                db_connection = NULL;
            }
        }
    }
    for (i=0; i<MAX_SQL_TEMPLATE; ++i)
    {
        if (query_info_array[i] != NULL)
            _query_info_array[i] = new struct QueryInfo(*query_info_array[i]);
        if (update_info_array[i] != NULL)
            _update_info_array[i] = new struct UpdateInfo(*update_info_array[i]);
    }

    _md5_sum = md5_sum;
    MYLOG_INFO("loaded %s[%s] successfully\n", filepath.c_str(), _md5_sum.c_str());
    return true;
}

sys::DBConnection* CConfigLoader::get_db_connection(int index) const
{
    if ((index < 0) || (index >= MAX_DB_CONNECTION))
        return NULL;

    if (NULL == g_db_connection[index])
        g_db_connection[index] = init_db_connection(index, true);

    return g_db_connection[index];
}

bool CConfigLoader::get_query_info(int index, struct QueryInfo* query_info) const
{
    sys::ReadLockHelper read_lock(_read_write_lock);
    if ((index < 0) || (index >= MAX_SQL_TEMPLATE))
        return false;
    if (NULL == _query_info_array[index])
        return false;

    *query_info = *(_query_info_array[index]);
    return true;
}

bool CConfigLoader::get_update_info(int index, struct UpdateInfo* update_info) const
{
    sys::ReadLockHelper read_lock(_read_write_lock);
    if ((index < 0) || (index >= MAX_SQL_TEMPLATE))
        return false;
    if (NULL == _update_info_array[index])
        return false;

    *update_info = *(_update_info_array[index]);
    return true;
}

bool CConfigLoader::get_db_info(int index, struct DbInfo* db_info) const
{
    sys::ReadLockHelper read_lock(_read_write_lock);

    if ((index < 0) || (index >= MAX_SQL_TEMPLATE))
        return false;
    if (NULL == _db_info_array[index])
        return false;

    *db_info = *(_db_info_array[index]);
    return true;
}

bool CConfigLoader::load_database(const Json::Value& json, struct DbInfo* db_info_array[])
{
    for (int i=0; i<static_cast<int>(json.size()); ++i)
    {
        struct DbInfo* db_info = new struct DbInfo(json[i]);

        if (!db_info->check())
        {
            delete db_info;
            return false;
        }
        else
        {
            MYLOG_INFO("%s\n", db_info->str().c_str());
            if (!add_db_info(db_info, db_info_array))
            {
                delete db_info;
                return false;
            }
        }
    }

    return true;
}

bool CConfigLoader::load_query(const Json::Value& json, struct QueryInfo* query_info_array[])
{
    for (int i=0; i<static_cast<int>(json.size()); ++i)
    {
        struct QueryInfo* query_info = new struct QueryInfo(json[i]);

        if (!query_info->check())
        {
            delete query_info;
            return false;
        }
        else
        {
            MYLOG_INFO("%s\n", query_info->str().c_str());
            if (!add_query_info(query_info, query_info_array))
            {
                delete query_info;
                return false;
            }
        }
    }

    return true;
}

bool CConfigLoader::load_update(const Json::Value& json, struct UpdateInfo* update_info_array[])
{
    for (int i=0; i<static_cast<int>(json.size()); ++i)
    {
        struct UpdateInfo* update_info = new struct UpdateInfo(json[i]);

        if (!update_info->check())
        {
            delete update_info;
            return false;
        }
        else
        {
            MYLOG_INFO("%s\n", update_info->str().c_str());
            if (!add_update_info(update_info, update_info_array))
            {
                delete update_info;
                return false;
            }
        }
    }

    return true;
}

bool CConfigLoader::add_db_info(struct DbInfo* db_info, struct DbInfo* db_info_array[])
{
    int index = db_info->index;

    if (index >= MAX_DB_CONNECTION)
    {
        MYLOG_ERROR("index[%d] greater or equal %d of %s\n", index, MAX_DB_CONNECTION, db_info->str().c_str());
        return false;
    }
    if (db_info_array[index] != NULL)
    {
        MYLOG_ERROR("index[%d] repeat: %s => %s\n", index, db_info->str().c_str(), db_info_array[index]->str().c_str());
        return false;
    }

    db_info_array[index] = db_info;
    return true;
}

bool CConfigLoader::add_query_info(struct QueryInfo* query_info, struct QueryInfo* query_info_array[])
{
    int index = query_info->index;

    if (index >= MAX_SQL_TEMPLATE)
    {
        MYLOG_ERROR("index[%d] greater or equal %d of %s\n", index, MAX_SQL_TEMPLATE, query_info->str().c_str());
        return false;
    }
    if (query_info_array[index] != NULL)
    {
        MYLOG_ERROR("index[%d] repeat: %s => %s\n", index, query_info->str().c_str(), query_info_array[index]->str().c_str());
        return false;
    }

    query_info_array[index] = query_info;
    return true;
}

bool CConfigLoader::add_update_info(struct UpdateInfo* update_info, struct UpdateInfo* update_info_array[])
{
    int index = update_info->index;

    if (index >= MAX_SQL_TEMPLATE)
    {
        MYLOG_ERROR("index[%d] greater or equal %d of %s\n", index, MAX_SQL_TEMPLATE, update_info->str().c_str());
        return false;
    }
    if (update_info_array[index] != NULL)
    {
        MYLOG_ERROR("index[%d] repeat: %s => %s\n", index, update_info->str().c_str(), update_info_array[index]->str().c_str());
        return false;
    }

    update_info_array[index] = update_info;
    return true;
}

sys::DBConnection* CConfigLoader::init_db_connection(int index, bool need_lock) const
{
    if (!need_lock)
    {
        return do_init_db_connection(index);
    }
    else
    {
        sys::ReadLockHelper read_lock(_read_write_lock);
        return do_init_db_connection(index);
    }
}

sys::DBConnection* CConfigLoader::do_init_db_connection(int index) const
{
    const struct DbInfo* _db_info = _db_info_array[index];
    sys::DBConnection* db_connection = new sys::CMySQLConnection;

    if (NULL == db_connection)
    {
        MYLOG_ERROR("can not create MySQL connection by 'mysql_connection'\n");
    }
    else
    {
        db_connection->set_host(_db_info->host, (uint16_t)_db_info->port);
        db_connection->set_user(_db_info->user, _db_info->password);
        db_connection->set_db_name(_db_info->name);
        db_connection->set_charset(_db_info->charset);
        db_connection->enable_auto_reconnect();

        try
        {
            db_connection->open();
        }
        catch (sys::CDBException& db_ex)
        {
            MYLOG_ERROR("connect %s failed: %s\n", _db_info->str().c_str(), db_ex.str().c_str());
            delete db_connection;
            db_connection = NULL;
        }
    }

    return db_connection;
}

} // namespace db_proxy
} // namespace mooon
