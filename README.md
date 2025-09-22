
# Butch

> A heavy lexer+parser.

## Concepts

Butch doesn't really work with tokens and abstract syntax tree nodes (AST), but instead works with <u>units</u>. Units are both tokens (when they have no children\*) and AST nodes. A token kind of unit can be sub-parsed and become a AST node for example.

<u>Rules</u> are match functions that take any kind of input, and try to match it against their ruleset, and spit out a unit the specified output class. Rules can contain classes, strings, regexes, aliases and symbols.

<u>Classes</u> are the identifier for what a unit represents. You can see units as instances of classes.

<u>Symbols</u> are characters like `(` and `!` that lets you do non-deterministic parsing of your units.

<u>`Literal`</u> is the default class that represents unparsed code (it cannot be accessed within a piece of butch code).

Butch simply creates a big unit that contains the whole code, and then tries to apply every rules it can until none match (a limited amount of attempts can be specified [TODO]). Some rules will split the big string into `["what's before", TheOutputClass, "what's after"]` (tokenizer-like behavior). Other rules will group classes and literals together (parser-like behavior). Butch choses which rules do what, and you only have to specify transformations ("if the input looks like that, then we output this").

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

### Rules
Rules look like this:
```
  OutputClass = <ruleset>
```
With `<ruleset>` being "word boundary" separated classes, strings, regexes symbols or aliases. Spaces aren't necessary around the colon character.

Examples:
```
Name = '\w+'
Comma = ","
List = Name ((Colon Name)+ |)
```

> Classes in a ruleset don't need to first be declared as the output of a rule. A name resolution pass is done after the butch code is read.

You can copy a ruleset of another rule using this syntax (this is useless on its own):
```
Name = '\w+'
NameThe2nd : Name
```

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
-->

### Contexts [TODO]

Contexts are what enables depth awareness with parenthesis, braces, brackets, strings, etc... Contexts are collections of rules. There's the global context (which is the one selected for every rule by default). A context begins and ends with rules called `Begin` and `End`:
```
= PARENTHESIS
  Begin = "("
  End = ")"
  MyCustomRuleThatAppliesOnlyBetweenParenthesis = "hello"
```
> Indentation isn't necessary.

The rules within a context won't be tested unless a tested rules indicates the start of this context. Here's an example:

```
Name = '\w+'
Call = Name ARGUMENTS
= ARGUMENTS
  Begin = "("
  End = ")"
```

Contexts require that the unit that should match ARGUMENTS is a literal. AND it's important to note that regex matches aren't supported (only one single string match per Begin/End).

The goal once a context is reached in a ruleset is to first define the start and the end of this context, then the other specified rules are ran once, in order of specification. Thus, only the `Begin` and `End` rules are tested out of order.

Depth awareness can also mix multiple context begin and end for a single context. For example, you could imagine that you want a specific context for parenthesis, braces and brackets, and that a mix of these three can be contained in each of them. Here's how you'd write that:

```
= PARENTHESIS : BRACES BRACKETS
  Begin = "("
  End = ")"
= BRACES : PARENTHESIS BRACKETS
  Begin = "{"
  End = "}"
= BRACKETS : BRACES BRACKETS
  Begin = "["
  End = "]"
```










