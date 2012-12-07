// @@Example: ex_cpp_dyn_query_equals @@
// @@Fold@@
#include <tightdb.hpp>

using namespace tightdb;
using namespace std;

int main()
{
    Group group;
    TableRef table = group.get_table("test");

    Spec& s = table->get_spec();
    s.add_column(COLUMN_TYPE_STRING, "name");
    s.add_column(COLUMN_TYPE_INT,    "age");
    s.add_column(COLUMN_TYPE_BOOL,   "male");
    s.add_column(COLUMN_TYPE_DATE,   "hired");
    s.add_column(COLUMN_TYPE_BINARY, "photo");
    table->update_from_spec();

// @@EndFold@@
    table->add_empty_row(2);
    
    table->set_string(0, 0, "Mary");
    table->set_int(1, 0, 28);
    table->set_bool(2, 0, false);
    table->set_date(3, 0, 50000);
    table->set_binary(4, 0, "bin \0 data 1", 12);

    table->set_string(0, 1, "Frank");
    table->set_int(1, 1, 56);
    table->set_bool(2, 1, true);
    table->set_date(3, 1, 60000);
    table->set_binary(4, 1, "bin \0 data 2", 12);

    // Find rows where name (column 0) == "Frank" 
    TableView view1 = table->where().equal(0, "Frank").find_all();
    assert(view1.size() == 1 && !strcmp(view1.get_string(0, 0), "Frank"));

    // Find rows where age (column 1) == 56
    TableView view2 = table->where().equal(1, int64_t(56)).find_all();
    assert(view2.size() == 1 && !strcmp(view2.get_string(0, 0), "Frank"));

    // Find rows where male (column 2) == true
    TableView view3 = table->where().equal(2, true).find_all();
    assert(view3.size() == 1 && !strcmp(view3.get_string(0, 0) == "Frank"));

    // Find people where hired (column 3) == 50000
    TableView view4 = table->where().equal(3, tightdb::Date(50000).get_date()).find_all(); 
    assert(view4.size() == 1 && !strcmp(view4.get_string(0, 0), "Mary"));

    // Find people where photo (column 4) equals the binary data "bin \0\n data 1"
    TableView view5 = table->where().equal(4, BinaryData("bin \0 data 1", 12)).find_all();
    assert(view5.size() == 1 && !strcmp(view5.get_string(0, 0), "Mary"));
}
// @@EndFold@@
// @@EndExample@@