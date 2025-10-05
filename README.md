
# Butch

> A heavy lexer+parser.

## Concepts

### Lexic

Butch doesn't really work with tokens and abstract syntax tree nodes (AST), but instead works with <u>units</u>. Units are both tokens (when they have no children\*) and AST nodes. A token kind of unit can be sub-parsed and become a AST node for example.

<u>Rules</u> are functions that take a list of units as input, try to match them against their ruleset, and if there's a match, spits out a unit with the specified output class . Custom side effects can be added in case there's a match. Rules can contain classes, strings, regexes, aliases and symbols.

<u>Classes</u> are the identifier for what a unit represents. You can see units as instances of classes.

<u>Symbols</u> are characters like `(` and `!` that lets you do non-deterministic parsing of your units.

<u>Literal</u> is the default class that represents unparsed code (it cannot be accessed within butch code).

<u>Contexts</u> are groups of rules that are only checked if the context is "pushed" (either by context push declaration or with a rule side effect). They're the one of the reasons Butch isn't a context-free parser (which is why they're named "contexts"). Typically, matching groups of braces together is done with contexts.

### How it works

Butch simply creates a big unit that contains the whole code, and then tries to apply every rules declared in the global context until none match (a limited amount of attempts can be specified [TODO]). Some rules will split the big string into `["what's before", TheOutputClass, "what's after"]` (these rules are called "token-like" rules). Other rules will group classes and literals together (parser-like behavior). Butch choses which rules do what, and you only have to specify transformations ("if the input looks like that, then we output this").

## Syntax

Slightly bnf inspired, but with elements from regex to allow for complex
pattern matching, and with depth awareness for brackets and strings (or
anything similar to that in your own language). Here are the basics:

### Strings

Strings symbolize an exact match. They're written between double quotes (`"`) and include every basic feature you'd expect from a string:
- You can escape characters using the backslash (`\"` and `\\`).
- You can insert special characters using their common escape character:
  | Character | Name |
  |-|-|
  | `\n` | Line feed |
  | `\r` | Carriage return |
  | `\a` | Bell |
  | `\e` | Escape |
  | `\b` | Backspace |
  | `\t` | Tab |
- You can insert arbitrary bytes (the 0 byte will end the string match where you put it): `\xDD` with `D` being an hexadecimal digit.

Strings can include actual line breaks, but it will be understood as a line break match (do not use this as a feature, except if you're trying to identify encoding or something).

### Regexes

Regex matches are exactly like strings except that escaped characters are treated by the regex engine, and start and end with single quotes instead of the double ones.

### Classes

Classes are simple names, use whatever convention you want, but I suggest using pascal case (that's what you'll see in the examples). Classes can contain numbers (yes, even as their first character), and can even just be numbers. You can also add in special characters that aren't considered "symbols", but I advise against this because butch will evolve and might start using new symbols.

### Symbols

Here is a table of all of the current butch symbols and what they mean:

| Symbol | Meaning |
|-|-|
| `(` | Begin of a grouping. |
| `)` | End of a grouping. |
| `\|` | Disjunction in the current grouping. |
| `!` | Match everything except the class, string, regex or alias that follows. |
| `+` | Match at least once the previous class, string, regex, alias or grouping. |
| `[` | "Prepreep": everything that's before won't be included as children of the output unit. |
| `]` | "Postpeep": everything that's after won't be included as children of the output unit. |
| `{` | Side effects declaration begin (cannot be recursed) |
| `}` | Side effects declaration end |

### Rules
Rules look like this:
```
  OutputClass = ruleset
```
With `ruleset` being "word boundary" separated classes, strings, regexes symbols or aliases. Spaces aren't necessary around the colon character.

Examples:
```
Name = '\w+'
Comma = ","
List = Name ((Colon Name)+ |)
```

> Classes in a ruleset don't need to first be declared as the output of a rule. A name resolution pass is done after the butch code is read.

Rules (non token-like) are, by default, checked from left to right, but can be checked right to left with this syntax:
```
RightToLeft < = A B C
LeftToRightByDefault = D E F
LeftToRightSpecified > = G H I
```

> The space, again, is not necessary.

### Aliases

Aliases are disjunctive lists of classes or aliases. Here's an example of how you write an alias:

```
?MyAlias = Option1 Option2 ?Option3
```

Where `?MyAlias` is an alternative name for the classes `Option1`, `Option2`, and for the alias `?Option3`. Aliases cannot contain symbols, strings, or regexes. Also, please be careful not to create infinite recursion:
```
?Wawa = ?Wawa
```
Because this code will "compile", but when trying to parse code it will loop indefinitely.

Aliases aren't classes in the sense that a unit cannot have an alias as its class. Aliases are only useful when you want to abreviate `(Option1 | Option2 | ...)` and/or avoid redunduncy. And because they require a much less complex pattern matching logic they're also faster.

> Note that aliases with only one element are "true aliases" in the sense that they're only a renaming of the element.

### Contexts [TODO]

Contexts are what enables depth awareness with parenthesis, braces, brackets, strings, etc... Contexts are collections of rules. There's the global context (which is the one selected for every rule by default). A context begins and ends with rules called `Begin` and `End`:
```
= PARENTHESIS
  Begin = "("
  End = ")"
  MyCustomRuleThatAppliesOnlyBetweenParenthesis = "hello"
```
> Indentation isn't necessary.

The rules within a context won't be tested unless a context rule is declared within a context. Here's an example:

```
Name = '\w+'
! ARGUMENTS # new rule with the 'ARGUMENT' class as an output
Call = Name ARGUMENTS
= ARGUMENTS
  Begin = "("
  End = ")"
```

A context match kind of rule works by first looking for the start and the end of this context, and then the other specified rules are ran over and over again, until none match (or all the rules are tried `Parser.rule_test_max` times if the value is greater than 0), in order of specification. Thus, only the `Begin` and `End` rules are tested out of order (and they do not produce any unit).

Contexts require that the unit that should match ARGUMENTS is a literal. The literal is first analyzed character by character, while trying to find groups of `Begin` and `End`. If an `End` is found with no matching `Begin` (this can happen with mixed contexts, see further down below), the task of matching the context fails, and the unit is tagged with `Unit::ERROR_CONTEXT_MISMATCH` as its id, instead of `Unit::LITERAL`. Also, the `Begin` and `End` rules have the special property that they only match at the begining of the literal. That is, with regexes, it's equivalent as adding a `^` at the begining of your pattern.

> Note: `^` and `$` in regexes are supposed to mean begin and end of literal, contrary to the standard "begin and end of a line" (the implementation can ignore this rule, although it is discouraged).

Depth awareness can also mix multiple context begins and ends for a single context (that's mixed contexts). For example, you could imagine that you want a specific context for parenthesis, braces and brackets, and that a mix of these three can be contained in each of them. Here's how you'd write that:

```
= PARENTHESIS BRACES BRACKETS
  Begin = "("
  End = ")"
= BRACES PARENTHESIS BRACKETS
  Begin = "{"
  End = "}"
= BRACKETS BRACES BRACKETS
  Begin = "["
  End = "]"
```

Sometimes it can be useful to include rules of other contexts within the current context without having to rewrite it. Here's an example of how to do that:

```
= PARENTHESIS BRACES
  Begin = "("
  End = ")"
  Separator = ","
= BRACES PARENTHESIS
  Begin = "{"
  End = "}"
  + Separator
```

You can write `+ <OutputClassName>` to copy the rule of another context within the current context. The referenced rule must alredy exist, and the first rule found with the given output class will be selected. The output class of the newly created rule will be the exact same as the copied one. You can also chain multiple elements after the `+` separating each by a space: `+ MyClass1 MyClass2 MyClass3 ...`. In this case, classes are copied in order of declaration, from left to right.

> Note that contexts aren't scopes for rules. Rules present within a context can be freely accessed from any other contexts.

In case you want to copy an entire context, you can simply use the context name, or for the global context you can just use `+` and end the line there. To be able to handle this behavior, when using the `+`, contexts are checked first and rules second. Which means that you cannot copy a context push. Here's a more complex example:

```
! BRACKETS
! PARENTHESIS
Name = '[a-zA-Z_][a-zA-Z0-9_]*'
Int = '[-+]?[0-9]+'
StatementEnd = ";"
Eq = "="
Indexing = Name BRACKETS
?Type = Name Indexing # simple type or array of a type
?Atom = Name Indexing Int
?Variable = ?Type # for some weird reason here it happens to be the same
VarDecl = ?Type Name
Assignement <= ?Variable Eq ?Atom
Statement = VarDecl StatementEnd
= PARENTHESIS BRACKETS
  Begin = "("
  End = ")"
  + Name Int VarDecl
= BRACKETS PARENTHESIS
  Begin = "["
  End = "]"
  +
  + PARENTHESIS
```

> Specifying both `Name` and `VarDecl` in the `PARENTHESIS` context is necessary

### Side effects [TODO]

When declaring a rule, you might want to add some side effects to the output if the rule successfuly matched. This can be achieved with curly braces put on the right of the ruleset element you want to add side effects to. For example, if you want to remove a unit once the rule is matched, you can use the `del` keyword:
```
StatementEnd = ";"
Int = '[0-9]+'
Statement = Int StatementEnd{del}
```

Side effects provide a very basic way to add, check, verify contextual information, and optimize the output of the parser. Here is the list of keywords and syntaxes you can use, separating each by a comma:

- `del` : Removes the unit, incompatible with every other non-conditional keyword. 
- `ctx <context>` : Transforms the corresponding unit into a literal and checks every rule from `<context>`. Incompatible with every other non-conditional keyword.
- `to <new_name>` : Gives a new name to the unit (creates the class accordingly if needed).
- *`check <expression>` : Additional check. Must be put before every other side effect. If `<expression>` is 0, the match success will be canceled.
- *`if <expression>` : Sets the `cmp` flag to 1 if `<expression>` is non-zero. Sets it to 0 otherwise. By default the `cmp` flag is set to 1.
- *`then <side effect>` : Check the `cmp` flag, and if enabled, executes `<side effect>`.
- *`else <side effect>` : Check the `cmp` flag, and if disabled, executes `<side effect>`.
- *`call <symbol>` : Tries to find symbol `<symbol>`. On failure, will end parsing (unless `FLAG_IGNORE_MISSING_SYMBOLS` is on in the parser flags, in which case side effects will be ignored). On success, will call the symbol with a reference/pointer to the current parser, rule, and unit as arguments (in this order), expecting a boolean return value. The return value indicate failure of operation (false on success, true otherwise). If the return value happens to be true, the unit will have a valid `symbol` attribute set to the address of the found symbol, and its id will be set to `Unit::CALL_FAILURE`.
- *`try <symbol>` : Same as call, except that the `cmp` flag is set to the boolean inverse of the return value, and the id of the unit isn't set to `Unit::CALL_FAILURE` (although failure to find the symbol will result in end of parsing, or side effects skipping).
- **`istore <expression>` : Evaluates `<expression>` as a signed 64bit integer, and stores it into the `data` attribute of the unit.
- **`ustore <expression>` : Evaluates `<expression>` as an unsigned integer, and stores it into the `data` attribute of the unit.
- **`pstore <expression>` : Evaluates `<expression>` as an owned pointer (assumed to be allocated with `malloc/calloc/realloc`), and stores it into the `data` attribute of the unit (when the unit is destroyed, the pointer is freed with C's `free`).
- **`rstore <expression>` : Evaluates `<expression>` as a referenced pointer, and stores it into the `data` attribute of the unit (when the unit is destroyed, the pointer isn't freed).

> Side effects annoted with * might never be implemented, and ** have an even higher chance of never being implemented. Both will be removed from the documentation in such case.

## The interface

The files `readparse.cpp`, `readserialize.cpp`, and `deserializeparse.cpp` provide examples on how to use the butch library. But here's a quick explanation. Also, as a general note, there's no use of C++'s constructor and destructors, nor private members. You can do whatever you want with this code, and you have to manage the memory yourself. Everything that needs to be destroyed has a `destroy` method. This includes `Parser`, `list<T>` (which is different from `std::list<T>`). Everything is "moved" (instead of copied) by default, contrary to C++ standards (why on earth would you want an implicit copy of a list ???). Also, all allocations are done using `malloc/realloc` (and freeing is done with `free`).

### Reading butch code

The first step is to read a piece of butch code. To do so, you can either provide a string with the butch code, or the path to a file, to a custom constructor for the `Parser` class:

```cpp
#include "butch.hpp"
...
Result<Parser, Parser::Error> res = Parser::read(owned_ptr_to_the_code);
Result<Parser, Parser::Error> res = Parser::read_file(ref_ptr_to_path);
```

You can then check if the reading of your butch code failed using a simple if and switch statement:

```cpp
if (res.type == ERR)
{
  switch (res.err.type)
  {
    case DOUBLE_QUOTE_MISMATCH:
      puts("Double quote mismatch");
      break
    ...
  }
  // no need to destroy the parser here
}
else
{
  // the code seems legit !
  Parser& my_parser = res.ok;
  ...
}
```

> See `readparse.cpp` to see all possible errors that can occur (some are declared in the code but are for internal use only).

And that's it ! You could, for example, print the state of the parser, using the debug method:

```cpp
my_parser.debug(stdout);
```

And don't forget, if you're finished with your parser, to destroy it !

```cpp
my_parser.destroy();
```

### Serialization/Deserialization [TODO]

It can be useful to serialize your butch code in order to avoid the whole butch code parsing step, which can make you gain a bit of time, especially on big pieces of butch code. To serialize and deserialize your parser, you're offered 3 methods per task:

```cpp
// For serialization
size_t my_parser.serialize_file(FILE*)
size_t my_parser.serialize(void*& ptr)
size_t my_parser.serialize(
  size_t(*callback)(char* src, size_t len, void* _data),
  void* data
)
// And for deserialization
Result<Parser, Parser::Error> Parser::deserialize_file(FILE*)
Result<Parser, Parser::Error> Parser::deserialize(void*& ptr, size_t len)
Result<Parser, Parser::Error> Parser::deserialize(
  size_t(*callback)(void* dst, size_t len, void* _data),
  void* data
)
```
The callbacks can return a value different than the asked length (the `len` argument) to signal any error in the reading/writing process. Any error in the serialization process will return 0 as the serialized size. And the only error that can be returned by `Paser::deserialize` is `Parser::Error::DESERIALIZATION_ERROR`. 

### Parsing

Finally, to actually use your parser, 

## A few notes on this implementation

1. There are no checks to avoid doubles of rules, contexts, etc...
    - Copies of a rules in the same context will simply repeat the operation.
    - Copies of contexts have no effects (and simply uses more memory).
    - Copies of output class but with different rulesets in two rules allow different ways to produce a unit with the same class.
2. Whether a name references a context or a rule depends on where the name is:
    - As the output class of a rule: it references the rule itself.
    - In a ruleset: if it exists, the context push with this name in the current context, and if not a rule whose output class has this name.
    - In a copy (with the `+` operator at the start of a line: )

