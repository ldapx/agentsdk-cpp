# Code Style Guide

This project follows the GCC coding style convention for C++ code.
This document defines the style that should be used for new code.
When this document is more specific than generic GCC guidance, this document
takes precedence.

## General Rules

- Use explicit types by default.
- Use `auto` only when the type is excessively verbose and the declaration
  remains clear at a glance.
- Keep formatting consistent and predictable.
- Match the established style of the file when making localized changes.
- Keep comments and documentation synchronized with the code.

## Documentation

This project uses Sphinx/hawkmoth with reStructuredText markup.

- Use `/** ... */` for block documentation.
- The first sentence is used as the brief description automatically.
- Document public classes, structs, enums, free functions, and member
  functions.
- Document internal helpers when their contract or behavior is not obvious from
  the declaration.
- Include parameters, return values, ownership, side effects, and
  preconditions.
- Use reStructuredText field lists for parameters and return values.
- Use Sphinx C++ domain roles for cross-references in documentation.

### Hawkmoth documentation example

```cpp
/**
 * Loads a scene from disk.
 *
 * :param path: Path to the scene file.
 * :param out_scene: Destination scene object.
 * :return: ``true`` if loading succeeded, otherwise ``false``.
 */
bool load_scene (const std::filesystem::path &path, scene &out_scene);

/**
 * Determines how the viewport camera is selected.
 */
enum class camera_mode {
  /** Use the editor camera. */
  editor,

  /** Use the assigned entity camera. */
  entity
};
```

### Cross-referencing

Use Sphinx C++ domain roles for cross-referencing declarations in
documentation:

- `` :cpp:class:`name` `` for classes
- `` :cpp:struct:`name` `` for structs
- `` :cpp:func:`name` `` for functions
- `` :cpp:member:`name` `` for member variables
- `` :cpp:var:`name` `` for variables
- `` :cpp:type:`name` `` for type aliases
- `` :cpp:enum:`name` `` for enums
- `` :cpp:enumerator:`name` `` for enumerators

## Formatting

- Indent with 2 spaces.
- Do not use tabs for indentation.
- Put opening braces on their own line for functions, classes, structs,
  namespaces, and control flow.
- Use a space before `(` in function declarations, function definitions, and
  control statements.
- Keep pointer and reference qualifiers attached to the type.
- Break long declarations and parameter lists across lines in a readable way.
- Use blank lines to separate logical blocks.

## Naming

- Use `snake_case` for classes, structs, functions, methods, variables,
  namespaces, and enum values.
- Use descriptive names.
- Use short names only for conventional cases such as loop indices and template
  parameters.
- Use the `m_` prefix for private data members.
- Use `enum class` for enums.

## Types and `auto`

- Use explicit types for variables, parameters, return types, pointers,
  references, booleans, numeric values, strings, containers, and entity
  identifiers.
- Use `auto` for iterators, `entt` views, `entt` groups, and similarly verbose
  template-heavy types.
- Use explicit types for the values obtained from those views and groups.

## Functions

- Use descriptive parameter names.
- Use `const T &` for large read-only parameters.
- Use values for small inexpensive types.
- Mark single-argument constructors `explicit` unless implicit conversion is
  intended.
- Keep functions focused on a single task.
- Use early returns to keep control flow straightforward.
- Use output parameters only for additional outputs.

## Classes and Structs

- Use `struct` for passive data containers.
- Use `class` for types with invariants, encapsulation, or non-trivial
  behavior.
- Declare copy and move operations explicitly when ownership or lifetime rules
  apply.
- Keep public interfaces compact and clear.

## Ownership and Const Correctness

- Use references for required non-null parameters.
- Use pointers for optional non-owning relationships.
- Use `std::unique_ptr` for exclusive ownership.
- Use `std::shared_ptr` only when shared ownership is required.
- Mark methods `const` when they do not modify observable state.
- Use `const` on references and pointers to read-only data.

## Includes

- Use `#pragma once` in headers.
- Include the matching header first in each `.cpp` file.
- Include only what is required.
- Use forward declarations in headers when a full definition is not needed.
- Keep headers minimal and self-contained.

## Comments

- Use comments for contracts, assumptions, invariants, ordering requirements,
  and other non-obvious details.
- Keep comments concise and factual.
- Keep comments close to the code they describe.

## Example

```cpp
/**
 * Tracks assets known to the editor.
 */
class asset_database {
public:
  explicit asset_database (std::filesystem::path root_path);

  /**
   * Adds an asset path if it is not already present.
   *
   * :param path: Path relative to the asset root.
   * :return: ``true`` if the asset was added, otherwise ``false``.
   */
  bool add_asset (const std::filesystem::path &path);

  std::size_t get_asset_count () const;

private:
  std::filesystem::path m_root_path;
  std::vector<std::filesystem::path> m_asset_paths;
};

void
update_cameras (entt::registry &registry)
{
  auto view = registry.view<comp::transform, comp::camera> ();

  for (entt::entity entity : view) {
    comp::transform &transform = view.get<comp::transform> (entity);
    comp::camera &camera = view.get<comp::camera> (entity);

    if (!camera.enabled) {
      continue;
    }

    update_camera_matrices (entity, transform, camera);
  }
}
```

## References

- [Hawkmoth Syntax](https://jnikula.github.io/hawkmoth/stable/syntax.html) —
  Documentation comment format and info field lists.
- [Sphinx C++ Domain](https://www.sphinx-doc.org/en/master/usage/domains/cpp.html) —
  C++ directives and cross-referencing roles.
