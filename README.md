
# Butch

Butch is a contextual lexer+parser that's only limited by python-like syntaxes. Butch is both a standard and the provided implementation.

## Introduction

### When to use butch ?

Butch isn't intended to be as fast as a hand-written tokenizer+parser. Butch is only intended to be used as a tool to quickly write the lexer and the parser for your language and skip to the interesting part (scopes, typing system, etc...). I'd still like butch to be fast, but it seems impossible for it to be faster than a well thought tokenizer+parser hand made for one specific language.

### Lexic

Butch doesn't really work with tokens and abstract syntax tree nodes (AST), but instead works with <u>units</u>. Units are both tokens (when they have no children\*) and AST nodes. A token-like unit can be sub-parsed (with "reparsing") and become a AST node for example.

<u>Rules</u> are functions that take a list of units as input, try to match them against their ruleset, and if there's a match, spits out a unit with the specified output class and all of the elements contained in the match as children. Rules can contain classes, strings, regexes, aliases and symbols.

<u>Classes</u> are the identifier for what a unit represents. You can see units as instances of classes.

<u>Symbols</u> are characters like `(` and `!` that lets you do non-deterministic parsing of your code.

<u>Literal</u> is the default class that represents unparsed code (it cannot be accessed within butch code).

<u>Contexts</u> are groups of rules that are only checked if the context is "pushed" (either by context push declaration or with a context re-parsing). They're the one of the reasons Butch isn't a context-free parser (which is why they're named "contexts"). Typically, matching groups of braces together is done with contexts.

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

Classes are simple names, use whatever convention you want, but I suggest using PascalCase (that's what you'll see in the examples, and to keywords are constrained to this case - or similar ones). Classes can contain numbers (yes, even as their first character), and can even just be numbers. You can't add special symbols in classes (except in the case of aliases with the `?`).

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
| `{` | Contextual re-parsing (CrP) begin  |
| `}` | CrP end |

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

### Contexts

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
! ARGUMENTS # new rule with the 'ARGUMENT' class as an output
Name = '\w+'
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

The issue with this example is that depth awareness will take into account braces and brackets within parenthesis, but that these won't actually be matched. If you and to match braces and brackets within parenthesis (and not just to avoid matching for example `()`)

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

To handle string-like elements within a context, butch has a type of rule called "depth-stoppers". These rules have the same properties as `Begin` and `End` rules, that is, they must be token-like and are matched with the equivalent of the `^` regex symbol. When these rules are matched, the context matcher skips the match to ensure no partial sub-context match within the depth-stopper. For example, if you've got this bit of butch code:

```
! PARENTHESIS
String = '".*?[^\\]"'
= PARENTHESIS
  Begin = "("
  End = ")"
  + String
```

In this example, the piece of code `("(")` would detect a context begin/end mismatch. To handle such case, you can provide the name of a rule to serve as a "depth-stopper":

```
! PARENTHESIS
String = '".*?[^\\]"'
= PARENTHESIS ! String
  Begin = "("
  End = ")"
  + String
```

If, for example, you don't want to match strings in any context, and just have them serve as context stoppers (why not ?), here's how you can achieve this:

```
! PARENTHESIS
= PARENTHESIS ! String
  Begin = "("
  End = ")"
= Useless
  String = '".*?[^\\]"'
```

You'll note that contexts don't have to contain a begin and an end. In the [CrP](#contextual-re-parsing-crp) part you'll see another use for these context without `Begin/End`.

> Depth-stoppers aren't copied within the context ! If you want them in there, you've got to manually copy them with the copy operator.

### Contextual re-parsing (CrP) [TODO]

Sometimes, contexts aren't only defined by their begin and their end. Often in contextual parsing you'll need to check for surrounding tokens/nodes to then run a special parsing of some code. In butch, this is achieved with CrP. CrP contexts don't necessarily have a begin/end pair (although they technically can, but these two rules will be skipped when doing the CrP). Once a rule matched, each element of the matched that has a CrP specification will be transformed back into a literal, its children will be removed, and all of the rules of the context will be checked just like with a regular context.

Here's an example of how and when you could use CrP:

```
! PARENTHESIS
Name = '[a-zA-Z_]+'
FuncDecl = Name Name PARENTHESIS{FUNC_DECL_ARGS}
= PARENTHESIS
  Begin = "("
  End = ")"
= FUNC_DECL_ARGS
  + Name
  Sep = ","
  ArgDecl = Name Name
```

It's important to note that a context used with CrP doesn't necessarily have a rule associated with it. Because of that, the id of the unit that was transformed back into a literal will have the id of the previously matched rule. For example, with the previous example, when for `FuncDecl` there's a match, the output can look like this:

```
FuncDecl
  Name
  Name
  PARENTHESIS
    ArgDecl
    Sep
    ArgDecl
```

Where the id of the unit for the argument list isn't `FUNC_DECL_ARGS` (because the id of a unit is the index of a rule).

Also, you don't necessarily need to reparse a context. For example, if you want to reparse a token:

```
Float = '[+-]?([0-9_]*[.][0-9]+|[0-9]+[.])'{FLOAT}
= FLOAT
  Sign = '[+-]'
  Decimal = '[0-9]+'
  Point = "."
  Exponent = '[eE]' # won't ever appear in the children of Float
```

Obviously you cannot put CrP in a `Begin`, an `End`, an alias or a context rule.

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
3. Regexes use the modified ECMAScript regex grammar. For now the C++ implementation is used, but for (de)serialization reasons, a custom regex engine must be written (for now, deserialization requires the re-compilation of regexes, which is what takes the longest time).
4. You cannot negate a grouping. You can only negate (with `!`) a class.