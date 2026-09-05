#pragma once

#include <type_traits>

#include <QQmlEngine>
#include <QtGlobal>

// The four singletons main() owns and QML borrows: Colors, Router, Layouts and
// Panel.
//
// All four need the same three things, and they used to carry four copies of
// them -- which is three copies too many for an invariant this sharp. The
// relevant machinery is QQmlPrivate::singletonConstructionMode() in
// qqmlprivate.h, which chooses how the engine obtains a QML_SINGLETON:
//
//     if constexpr (std::is_default_constructible<T>::value)
//         return SingletonConstructionMode::Constructor;
//     if constexpr (HasSingletonFactory<T>::value)
//         return SingletonConstructionMode::Factory;
//
// Default-constructible is tested FIRST. So a singleton that can be
// default-constructed is default-constructed BY THE ENGINE, silently, and
// create() is never called -- no warning, no error, and no way to tell from
// the outside. That shipped once here: QML held a freshly built Theme whose
// QColor members were all default-invalid while main() held the configured
// one, every surface drew at #000000, and finding it took a pixel sample and
// an address comparison.
//
// MOARCHY_SINGLETON_INVARIANT below turns that into a build error. It belongs
// under every class using this base -- see the note at the bottom of theme.h
// for the full story.
//
// Inheriting create() rather than repeating it is safe by the same header's
// definition of a factory:
//
//     static constexpr bool value = std::is_same_v<
//         decltype(WrapperT::create(...)), T *>;
//
// CRTP satisfies it exactly: the base knows the derived type, so create()
// genuinely returns Derived *, and qualified lookup finds an inherited static.
template <class Derived>
class MainOwnedSingleton
{
public:
    static void setInstance(Derived *instance) { s_instance = instance; }

    static Derived *create(QQmlEngine *, QJSEngine *)
    {
        // NOT Q_ASSERT: it compiles out under NDEBUG, and the packaged build is
        // Release, so a null instance would be a segfault in the field and a
        // clean abort only on a developer's machine. Returning nullptr makes
        // QML report an unavailable singleton, which is loud and survivable.
        if (!s_instance) {
            qCritical("%s singleton used before main() set the instance",
                      Derived::staticMetaObject.className());
            return nullptr;
        }
        // The engine must never delete an object main owns.
        QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
        return s_instance;
    }

protected:
    MainOwnedSingleton() = default;

    // The invariant lives in the DESTRUCTOR, which is the only one of the three
    // candidate sites that works. Verified all three against Qt 6.11.2 / GCC 16:
    //
    //   in the class body   -- Derived is incomplete while its own base is being
    //                          instantiated, so this is a hard error even for a
    //                          correct class.
    //   in create()         -- exactly backwards. If the bug is present the
    //                          engine picks Constructor mode and never
    //                          instantiates create() at all, so the check would
    //                          fire only when it was not needed.
    //   in the destructor   -- Derived is complete, and the destructor is
    //                          instantiated in every translation unit that
    //                          builds or destroys one, including the class's own
    //                          .cpp where the vtable forces it.
    //
    // Being here rather than in a macro means a singleton added later cannot
    // forget it. MOARCHY_SINGLETON_INVARIANT below is the signpost next to each
    // class; this is the guarantee.
    ~MainOwnedSingleton()
    {
        static_assert(!std::is_default_constructible_v<Derived>,
                      "A MainOwnedSingleton must not be default-constructible: "
                      "the QML engine would build its own instance instead of "
                      "calling create(), and QML would then hold a different "
                      "object from the one main() configured.");
    }

private:
    inline static Derived *s_instance = nullptr;
};

// The invariant that keeps QML and main() looking at the same object. Place it
// after the class definition, which is where the type is complete.
#define MOARCHY_SINGLETON_INVARIANT(Type)                                        \
    static_assert(!std::is_default_constructible_v<Type>,                        \
                  #Type " must not be default-constructible: the QML engine "    \
                        "would build its own instance instead of calling "       \
                        "create(), and QML would then hold a different object "  \
                        "from the one main() configured.")
