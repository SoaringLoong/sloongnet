/*
 * @Author: WCB
 * @Date: 2020-04-21 19:41:30
 * @LastEditors: WCB
 * @LastEditTime: 2020-05-13 12:10:10
 * @Description: file content
 */
#include <string>
using namespace std;
#include <sqlite_orm.h>
using namespace sqlite_orm;

namespace Sloong
{
    const static string TEMPLATE_TBL_NAME = "template_list";

    const static string Default_Create_Database_File_SQL = 
"CREATE TABLE 'template_list' (\
  'id' integer NOT NULL,\
  'replicas' integer,\
  'name' TEXT,\
  'note' TEXT,\
  'reference' TEXT,\
  'configuation' blob,\
  PRIMARY KEY ('id')\
);";

    struct TemplateInfo
    {
        int id;
        int replicas;
        string name;
        string note;
        string reference;
        vector<char> configuation;
    };

    inline auto InitStorage(const string& path) {
        using namespace sqlite_orm;
        return make_storage(path,
            make_table(TEMPLATE_TBL_NAME,
                make_column("id", &TemplateInfo::id, autoincrement(), primary_key()),
                make_column("replicas", &TemplateInfo::replicas),
                make_column("name", &TemplateInfo::name),
                make_column("note", &TemplateInfo::note),
                make_column("reference", &TemplateInfo::reference),
                make_column("configuation", &TemplateInfo::configuation)
            )
        );
    }

    using Storage = decltype(InitStorage(""));
}