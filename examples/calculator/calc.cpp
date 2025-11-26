#include "../../src/butch.hpp"
using u32 = uint32_t;
using f32 = float;

#define D(name) \
  rule_id name
  D(Parenthesis); D(Number);
  D(Plus); D(Minus); D(Mult); D(Div);
  D(Signed); D(Op1); D(Op2);


f32 v2s(const char* s, const char* e)
{
  f32 x = 0.f;
  while (s != e && *s != '.')
  {
    if (!isdigit(*s))
      return 0.f;
    x = x*10.f + f32(*s - '0');
    s++;
  }
  if (s != e)
  {
    s++;
    f32 dv = 0.1f;
    while (s != e)
    {
      if (!isdigit(*s))
        return 0.f;
      x += f32(*s - '0') * dv;
      s++;
      dv *= 0.1f;
    }
  }
  return x;
}

f32 exec(Parser& p, const char* code, Unit units[], u32 uc)
{
  if (uc == 1) // number
  {
    if (!(units[0].is(p, Number)))
    {
      printf("expected number, got: %i\n", units[0].id);
      assert(false);
    }
    return v2s(code+units[0].s_start, code+units[0].s_end());
  }
  if (units[0].is(p, Parenthesis))
  {
    return exec(p, code, units, units[0].children_len);
  }
  for (u32 i = 0; i < uc; i += units[i].children_len+1)
  {
    Unit* op_l = units+i+1;
    Unit* oper = op_l+op_l->children_len+1;
    Unit* op_r = oper+oper->children_len+1;
    puts("left");
    f32 val_l = exec(p, code, op_l, op_l->children_len+1);
    puts("right");
    f32 val_r = exec(p, code, op_r, op_r->children_len+1);
    if (oper->is(p, Div))
     return val_l / val_r;
    else if (oper->is(p, Mult))
     return val_l * val_r;
    else if (oper->is(p, Minus))
     return val_l - val_r;
    else if (oper->is(p, Plus))
     return val_l + val_r;
    else
    {
      printf("Unknown operation %i\n", oper->id);
      assert(false);
    }
  }
  return 0.f;
}

int main(int argc, const char** argv)
{
  if (argc != 2)
  {
    puts("Syntax: calc <filename>");
    return 1;
  }

  auto p_res = Parser::read_file("calc.butch");
  if (p_res.type == ERR)
  {
    puts("Error in butch code");
    return 1;
  }
  Parser& p = p_res.ok;

#define S(name) \
  name = p.get_rule(#name)
  S(Parenthesis); S(Number);
  S(Plus); S(Minus); S(Mult); S(Div);
  S(Signed); S(Op1); S(Op2);

  Parser::FileParse fp = p.parse_file(argv[1]);

  if (fp.parsed_code == nullptr)
  { puts("Can't read file."); return 1; }

  p.debug(stdout);
  printf("=== Code: ===\n%s\n", fp.parsed_code);
  p.debug_output(stdout, {fp.output.units, fp.output.ucount});
  printf("Result: %f\n",
    exec(p, fp.parsed_code, fp.output.units, fp.output.ucount)
  );

  free(fp.parsed_code);
  
  return 0;
}

