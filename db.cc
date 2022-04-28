#include <string>
#include <memory>
#include <variant>
#include <map>
#include <vector>
#include <optional>

using namespace std;

struct Schema
{
    struct Column
    {
        string name;
        Type type;
    };

    vector<Column> columns;
    map<string, size_t> column_name_map;

    Column column(size_t index) const
    {
        return columns.at(index);
    }

    Column column(string name) const
    {
        return columns.at(column_name_map.at(name));
    }
};

struct Row
{
    map<string, Value> payload;
};

struct Expression
{
    virtual Value eval(const Row &row);
    virtual Type return_type(const Schema &schema);
};

struct Variable : Expression
{
    Variable(string _name) : name(_name) {}

    string name;

    Value eval(const Row &row) override
    {
        return row.payload.at(name);
    }

    Type return_type(const Schema &schema) override
    {
        return schema.column(name).type;
    }
};

struct EqualExpr : Expression
{
    EqualExpr(unique_ptr<EqualExpr> _left_child,
              unique_ptr<EqualExpr> _right_child) : left_child(move(_left_child)), right_child(move(_right_child))
    {
    }

    Value eval(const Row &row) override
    {
        auto left_result = left_child->eval(row);
        auto right_result = right_child->eval(row);
        return Value{static_cast<bool>(left_result.value == right_result.value)};
    }

    Type return_type(const Schema &schema) override
    {
        return Type::Boolean;
    }

    unique_ptr<EqualExpr> left_child;
    unique_ptr<EqualExpr> right_child;
};

enum Type
{
    Int,
    String,
    Boolean,
};

struct Value
{
    variant<int64_t, string, bool> value;

    int64_t get_int()
    {
        return get<int64_t>(value);
    }

    string get_string()
    {
        return get<string>(value);
    }

    bool get_boolean()
    {
        return get<bool>(value);
    }
};

struct Operator
{
    virtual void open()
    {
        for (auto child : children())
        {
            child->open();
        }
        open();
    }

    virtual optional<Row> next() = 0;

    virtual void close()
    {
        for (auto child : children())
        {
            child->close();
        }
        close();
    }

    virtual vector<Operator *> children()
    {
        return {};
    }
};

struct Scan : Operator
{
    Scan(unique_ptr<Operator> _child) : child(move(_child)) {}

    unique_ptr<Operator> child;

    optional<Row> next() override
    {
        return {};
    }
};

struct Filter : Operator
{
    Filter(unique_ptr<Expression> _pred, unique_ptr<Operator> child) : pred(move(_pred)) {}

    optional<Row> next() override
    {
        for (;;)
        {
            auto next = child->next();
            if (!next.has_value())
            {
                return {};
            }

            auto &row = next.value();
            auto result = pred->eval(row).get_boolean();
            if (result)
            {
                return row;
            }
        }
    }

    vector<Operator *> children() override
    {
        return {child.get()};
    }

    unique_ptr<Expression> pred;
    unique_ptr<Operator> child;
};

struct Project : Operator
{
    Project(vector<tuple<string, Expression>> _projects,
            unique_ptr<Operator> _child)
        : projects(move(_projects)),
          child(move(_child)) {}

    optional<Row> next() override
    {
        auto next = child->next();
        if (!next.has_value())
        {
            return {};
        }

        auto &row = next.value();
        auto new_row = Row{};

        for (auto &[name, expr] : projects)
        {
            auto result = expr.eval(row);
            new_row.payload[name] = result;
        }
        return new_row;
    }

    vector<Operator *> children() override
    {
        return {child.get()};
    }

    vector<tuple<string, Expression>> projects;
    unique_ptr<Operator> child;
};
