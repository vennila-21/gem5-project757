from __future__ import generators

import os
import re
import sys

#####################################################################
#
# M5 Python Configuration Utility
#
# The basic idea is to write simple Python programs that build Python
# objects corresponding to M5 SimObjects for the deisred simulation
# configuration.  For now, the Python emits a .ini file that can be
# parsed by M5.  In the future, some tighter integration between M5
# and the Python interpreter may allow bypassing the .ini file.
#
# Each SimObject class in M5 is represented by a Python class with the
# same name.  The Python inheritance tree mirrors the M5 C++ tree
# (e.g., SimpleCPU derives from BaseCPU in both cases, and all
# SimObjects inherit from a single SimObject base class).  To specify
# an instance of an M5 SimObject in a configuration, the user simply
# instantiates the corresponding Python object.  The parameters for
# that SimObject are given by assigning to attributes of the Python
# object, either using keyword assignment in the constructor or in
# separate assignment statements.  For example:
#
# cache = BaseCache('my_cache', root, size=64*K)
# cache.hit_latency = 3
# cache.assoc = 8
#
# (The first two constructor arguments specify the name of the created
# cache and its parent node in the hierarchy.)
#
# The magic lies in the mapping of the Python attributes for SimObject
# classes to the actual SimObject parameter specifications.  This
# allows parameter validity checking in the Python code.  Continuing
# the example above, the statements "cache.blurfl=3" or
# "cache.assoc='hello'" would both result in runtime errors in Python,
# since the BaseCache object has no 'blurfl' parameter and the 'assoc'
# parameter requires an integer, respectively.  This magic is done
# primarily by overriding the special __setattr__ method that controls
# assignment to object attributes.
#
# The Python module provides another class, ConfigNode, which is a
# superclass of SimObject.  ConfigNode implements the parent/child
# relationship for building the configuration hierarchy tree.
# Concrete instances of ConfigNode can be used to group objects in the
# hierarchy, but do not correspond to SimObjects themselves (like a
# .ini section with "children=" but no "type=".
#
# Once a set of Python objects have been instantiated in a hierarchy,
# calling 'instantiate(obj)' (where obj is the root of the hierarchy)
# will generate a .ini file.  See simple-4cpu.py for an example
# (corresponding to m5-test/simple-4cpu.ini).
#
#####################################################################

#####################################################################
#
# ConfigNode/SimObject classes
#
# The Python class hierarchy rooted by ConfigNode (which is the base
# class of SimObject, which in turn is the base class of all other M5
# SimObject classes) has special attribute behavior.  In general, an
# object in this hierarchy has three categories of attribute-like
# things:
#
# 1. Regular Python methods and variables.  These must start with an
# underscore to be treated normally.
#
# 2. SimObject parameters.  These values are stored as normal Python
# attributes, but all assignments to these attributes are checked
# against the pre-defined set of parameters stored in the class's
# _param_dict dictionary.  Assignments to attributes that do not
# correspond to predefined parameters, or that are not of the correct
# type, incur runtime errors.
#
# 3. Hierarchy children.  The child nodes of a ConfigNode are stored
# in the node's _children dictionary, but can be accessed using the
# Python attribute dot-notation (just as they are printed out by the
# simulator).  Children cannot be created using attribute assigment;
# they must be added by specifying the parent node in the child's
# constructor or using the '+=' operator.

# The SimObject parameters are the most complex, for a few reasons.
# First, both parameter descriptions and parameter values are
# inherited.  Thus parameter description lookup must go up the
# inheritance chain like normal attribute lookup, but this behavior
# must be explicitly coded since the lookup occurs in each class's
# _param_dict attribute.  Second, because parameter values can be set
# on SimObject classes (to implement default values), the parameter
# checking behavior must be enforced on class attribute assignments as
# well as instance attribute assignments.  Finally, because we allow
# class specialization via inheritance (e.g., see the L1Cache class in
# the simple-4cpu.py example), we must do parameter checking even on
# class instantiation.  To provide all these features, we use a
# metaclass to define most of the SimObject parameter behavior for
# this class hierarchy.
#
#####################################################################

# The metaclass for ConfigNode (and thus for everything that dervies
# from ConfigNode, including SimObject).  This class controls how new
# classes that derive from ConfigNode are instantiated, and provides
# inherited class behavior (just like a class controls how instances
# of that class are instantiated, and provides inherited instance
# behavior).
class MetaConfigNode(type):

    # __new__ is called before __init__, and is where the statements
    # in the body of the class definition get loaded into the class's
    # __dict__.  We intercept this to filter out parameter assignments
    # and only allow "private" attributes to be passed to the base
    # __new__ (starting with underscore).
    def __new__(cls, name, bases, dict):
        priv_keys = [k for k in dict.iterkeys() if k.startswith('_')]
        priv_dict = {}
        for k in priv_keys: priv_dict[k] = dict[k]; del dict[k]
        # entries left in dict will get passed to __init__, where we'll
        # deal with them as params.
        return super(MetaConfigNode, cls).__new__(cls, name, bases, priv_dict)

    # initialization: start out with an empty param dict (makes life
    # simpler if we can assume _param_dict is always valid).  Also
    # build inheritance list to simplify searching for inherited
    # params.  Finally set parameters specified in class definition
    # (if any).
    def __init__(cls, name, bases, dict):
        super(MetaConfigNode, cls).__init__(cls, name, bases, {})
        # initialize _param_dict to empty
        cls._param_dict = {}
        # __mro__ is the ordered list of classes Python uses for
        # method resolution.  We want to pick out the ones that have a
        # _param_dict attribute for doing parameter lookups.
        cls._param_bases = \
                         [c for c in cls.__mro__ if hasattr(c, '_param_dict')]
        # initialize attributes with values from class definition
        for (pname, value) in dict.items():
            try:
                setattr(cls, pname, value)
            except Exception, exc:
                print "Error setting '%s' to '%s' on class '%s'\n" \
                      % (pname, value, cls.__name__), exc

    # set the class's parameter dictionary (called when loading
    # class descriptions)
    def set_param_dict(cls, param_dict):
        # should only be called once (current one should be empty one
        # from __init__)
        assert not cls._param_dict
        cls._param_dict = param_dict
        # initialize attributes with default values
        for (pname, param) in param_dict.items():
            try:
                setattr(cls, pname, param.default)
            except Exception, exc:
                print "Error setting '%s' default on class '%s'\n" \
                      % (pname, cls.__name__), exc

    # Lookup a parameter description by name in the given class.  Use
    # the _param_bases list defined in __init__ to go up the
    # inheritance hierarchy if necessary.
    def lookup_param(cls, param_name):
        for c in cls._param_bases:
            param = c._param_dict.get(param_name)
            if param: return param
        return None

    # Set attribute (called on foo.attr_name = value when foo is an
    # instance of class cls).
    def __setattr__(cls, attr_name, value):
        # normal processing for private attributes
        if attr_name.startswith('_'):
            object.__setattr__(cls, attr_name, value)
            return
        # no '_': must be SimObject param
        param = cls.lookup_param(attr_name)
        if not param:
            raise AttributeError, \
                  "Class %s has no parameter %s" % (cls.__name__, attr_name)
        # It's ok: set attribute by delegating to 'object' class.
        # Note the use of param.make_value() to verify/canonicalize
        # the assigned value
        object.__setattr__(cls, attr_name, param.make_value(value))

    # generator that iterates across all parameters for this class and
    # all classes it inherits from
    def all_param_names(cls):
        for c in cls._param_bases:
            for p in c._param_dict.iterkeys():
                yield p

# The ConfigNode class is the root of the special hierarchy.  Most of
# the code in this class deals with the configuration hierarchy itself
# (parent/child node relationships).
class ConfigNode(object):
    # Specify metaclass.  Any class inheriting from ConfigNode will
    # get this metaclass.
    __metaclass__ = MetaConfigNode

    # Constructor.  Since bare ConfigNodes don't have parameters, just
    # worry about the name and the parent/child stuff.
    def __init__(self, _name, _parent=None):
        # Type-check _name
        if type(_name) != str:
            if isinstance(_name, ConfigNode):
                # special case message for common error of trying to
                # coerce a SimObject to the wrong type
                raise TypeError, \
                      "Attempt to coerce %s to %s" \
                      % (_name.__class__.__name__, self.__class__.__name__)
            else:
                raise TypeError, \
                      "%s name must be string (was %s, %s)" \
                      % (self.__class__.__name__, _name, type(_name))
        # if specified, parent must be a subclass of ConfigNode
        if _parent != None and not isinstance(_parent, ConfigNode):
            raise TypeError, \
                  "%s parent must be ConfigNode subclass (was %s, %s)" \
                  % (self.__class__.__name__, _name, type(_name))
        self._name = _name
        self._parent = _parent
        self._children = {}
        if (_parent):
            _parent.__addChild(self)
        # Set up absolute path from root.
        if (_parent and _parent._path != 'Universe'):
            self._path = _parent._path + '.' + self._name
        else:
            self._path = self._name

    # When printing (e.g. to .ini file), just give the name.
    def __str__(self):
        return self._name

    # Catch attribute accesses that could be requesting children, and
    # satisfy them.  Note that __getattr__ is called only if the
    # regular attribute lookup fails, so private and parameter lookups
    # will already be satisfied before we ever get here.
    def __getattr__(self, name):
        try:
            return self._children[name]
        except KeyError:
            raise AttributeError, \
                  "Node '%s' has no attribute or child '%s'" \
                  % (self._name, name)

    # Set attribute.  All attribute assignments go through here.  Must
    # be private attribute (starts with '_') or valid parameter entry.
    # Basically identical to MetaConfigClass.__setattr__(), except
    # this handles instances rather than class attributes.
    def __setattr__(self, attr_name, value):
        if attr_name.startswith('_'):
            object.__setattr__(self, attr_name, value)
            return
        # not private; look up as param
        param = self.__class__.lookup_param(attr_name)
        if not param:
            raise AttributeError, \
                  "Class %s has no parameter %s" \
                  % (self.__class__.__name__, attr_name)
        # It's ok: set attribute by delegating to 'object' class.
        # Note the use of param.make_value() to verify/canonicalize
        # the assigned value
        object.__setattr__(self, attr_name, param.make_value(value))

    # Add a child to this node.
    def __addChild(self, new_child):
        # set child's parent before calling this function
        assert new_child._parent == self
        if not isinstance(new_child, ConfigNode):
            raise TypeError, \
                  "ConfigNode child must also be of class ConfigNode"
        if new_child._name in self._children:
            raise AttributeError, \
                  "Node '%s' already has a child '%s'" \
                  % (self._name, new_child._name)
        self._children[new_child._name] = new_child

    # operator overload for '+='.  You can say "node += child" to add
    # # a child that was created with parent=None.  An early attempt
    # at # playing with syntax; turns out not to be that useful.
    def __iadd__(self, new_child):
        if new_child._parent != None:
            raise AttributeError, \
                  "Node '%s' already has a parent" % new_child._name
        new_child._parent = self
        self.__addChild(new_child)
        return self

    # Print instance info to .ini file.
    def _instantiate(self):
        print '[' + self._path + ']'	# .ini section header
        if self._children:
            # instantiate children in sorted order for backward
            # compatibility (else we can end up with cpu1 before cpu0).
            child_names = self._children.keys()
            child_names.sort()
            print 'children =',
            for child_name in child_names:
                print child_name,
            print
        self._instantiateParams()
        print
        # recursively dump out children
        if self._children:
            for child_name in child_names:
                self._children[child_name]._instantiate()

    # ConfigNodes have no parameters.  Overridden by SimObject.
    def _instantiateParams(self):
        pass

# SimObject is a minimal extension of ConfigNode, implementing a
# hierarchy node that corresponds to an M5 SimObject.  It prints out a
# "type=" line to indicate its SimObject class, prints out the
# assigned parameters corresponding to its class, and allows
# parameters to be set by keyword in the constructor.  Note that most
# of the heavy lifting for the SimObject param handling is done in the
# MetaConfigNode metaclass.

class SimObject(ConfigNode):
    # initialization: like ConfigNode, but handle keyword-based
    # parameter initializers.
    def __init__(self, _name, _parent=None, **params):
        ConfigNode.__init__(self, _name, _parent)
        for param, value in params.items():
            setattr(self, param, value)

    # print type and parameter values to .ini file
    def _instantiateParams(self):
        print "type =", self.__class__._name
        for pname in self.__class__.all_param_names():
            value = getattr(self, pname)
            if value != None:
                print pname, '=', value

#####################################################################
#
# Parameter description classes
#
# The _param_dict dictionary in each class maps parameter names to
# either a Param or a VectorParam object.  These objects contain the
# parameter description string, the parameter type, and the default
# value (loaded from the PARAM section of the .odesc files).  The
# make_value() method on these objects is used to force whatever value
# is assigned to the parameter to the appropriate type.
#
# Note that the default values are loaded into the class's attribute
# space when the parameter dictionary is initialized (in
# MetaConfigNode.set_param_dict()); after that point they aren't
# used.
#
#####################################################################

# Force parameter value (rhs of '=') to ptype (or None, which means
# not set).
def make_param_value(ptype, value):
    # nothing to do if None or already correct type
    if value == None or isinstance(value, ptype):
        return value
    # this type conversion will raise an exception if it's illegal
    return ptype(value)

# Regular parameter.
class Param(object):
    # Constructor.  E.g., Param(Int, "number of widgets", 5)
    def __init__(self, ptype, desc, default=None):
        self.ptype = ptype
        self.desc = desc
        self.default = default

    # Convert assigned value to appropriate type.
    def make_value(self, value):
        return make_param_value(self.ptype, value)

# The _VectorParamValue class is a wrapper for vector-valued
# parameters.  The leading underscore indicates that users shouldn't
# see this class; it's magically generated by VectorParam.  The
# parameter values are stored in the 'value' field as a Python list of
# whatever type the parameter is supposed to be.  The only purpose of
# storing these instead of a raw Python list is that we can override
# the __str__() method to not print out '[' and ']' in the .ini file.
class _VectorParamValue(object):
    def __init__(self, list):
        self.value = list

    def __str__(self):
        return ' '.join(map(str, self.value))

# Vector-valued parameter description.  Just like Param, except that
# the value is a vector (list) of the specified type instead of a
# single value.
class VectorParam(object):
    # Constructor.  The resulting parameter will be a list of ptype.
    def __init__(self, ptype, desc, default=None):
        self.ptype = ptype
        self.desc = desc
        self.default = default

    # Convert assigned value to appropriate type.  If the RHS is not a
    # list or tuple, it generates a single-element list.
    def make_value(self, value):
        if value == None: return value
        if isinstance(value, list) or isinstance(value, tuple):
            # list: coerce each element into new list
            val_list = [make_param_value(self.ptype, v) for v in
                        iter(value)]
        else:
            # singleton: coerce & wrap in a list
            val_list = [ make_param_value(self.ptype, value) ]
        # wrap list in _VectorParamValue (see above)
        return _VectorParamValue(val_list)

#####################################################################
#
# Parameter Types
#
# Though native Python types could be used to specify parameter types
# (the 'ptype' field of the Param and VectorParam classes), it's more
# flexible to define our own set of types.  This gives us more control
# over how Python expressions are converted to values (via the
# __init__() constructor) and how these values are printed out (via
# the __str__() conversion method).  Eventually we'll need these types
# to correspond to distinct C++ types as well.
#
#####################################################################

# Integer parameter type.
class Int(object):
    # Constructor.  Value must be Python int or long (long integer).
    def __init__(self, value):
        t = type(value)
        if t == int or t == long:
            self.value = value
        else:
            raise TypeError, "Int param got value %s %s" % (repr(value), t)

    # Use Python string conversion.  Note that this puts an 'L' on the
    # end of long integers; we can strip that off here if it gives us
    # trouble.
    def __str__(self):
        return str(self.value)

# Counter, Addr, and Tick are just aliases for Int for now.
class Counter(Int):
    pass

class Addr(Int):
    pass

class Tick(Int):
    pass

# Boolean parameter type.
class Bool(object):

    # Constructor.  Typically the value will be one of the Python bool
    # constants True or False (or the aliases true and false below).
    # Also need to take integer 0 or 1 values since bool was not a
    # distinct type in Python 2.2.  Parse a bunch of boolean-sounding
    # strings too just for kicks.
    def __init__(self, value):
        t = type(value)
        if t == bool:
            self.value = value
        elif t == int or t == long:
            if value == 1:
                self.value = True
            elif value == 0:
                self.value = False
        elif t == str:
            v = value.lower()
            if v == "true" or v == "t" or v == "yes" or v == "y":
                self.value = True
            elif v == "false" or v == "f" or v == "no" or v == "n":
                self.value = False
        # if we didn't set it yet, it must not be something we understand
        if not hasattr(self, 'value'):
            raise TypeError, "Bool param got value %s %s" % (repr(value), t)

    # Generate printable string version.
    def __str__(self):
        if self.value: return "true"
        else: return "false"

# String-valued parameter.
class String(object):
    # Constructor.  Value must be Python string.
    def __init__(self, value):
        t = type(value)
        if t == str:
            self.value = value
        else:
            raise TypeError, "String param got value %s %s" % (repr(value), t)

    # Generate printable string version.  Not too tricky.
    def __str__(self):
        return self.value


# Enumerated types are a little more complex.  The user specifies the
# type as Enum(foo) where foo is either a list or dictionary of
# alternatives (typically strings, but not necessarily so).  (In the
# long run, the integer value of the parameter will be the list index
# or the corresponding dictionary value.  For now, since we only check
# that the alternative is valid and then spit it into a .ini file,
# there's not much point in using the dictionary.)

# What Enum() must do is generate a new type encapsulating the
# provided list/dictionary so that specific values of the parameter
# can be instances of that type.  We define two hidden internal
# classes (_ListEnum and _DictEnum) to serve as base classes, then
# derive the new type from the appropriate base class on the fly.


# Base class for list-based Enum types.
class _ListEnum(object):
    # Constructor.  Value must be a member of the type's map list.
    def __init__(self, value):
        if value in self.map:
            self.value = value
            self.index = self.map.index(value)
        else:
            raise TypeError, "Enum param got bad value '%s' (not in %s)" \
                  % (value, self.map)

    # Generate printable string version of value.
    def __str__(self):
        return str(self.value)

class _DictEnum(object):
    # Constructor.  Value must be a key in the type's map dictionary.
    def __init__(self, value):
        if value in self.map:
            self.value = value
            self.index = self.map[value]
        else:
            raise TypeError, "Enum param got bad value '%s' (not in %s)" \
                  % (value, self.map.keys())

    # Generate printable string version of value.
    def __str__(self):
        return str(self.value)

# Enum metaclass... calling Enum(foo) generates a new type (class)
# that derives from _ListEnum or _DictEnum as appropriate.
class Enum(type):
    # counter to generate unique names for generated classes
    counter = 1

    def __new__(cls, map):
        if isinstance(map, dict):
            base = _DictEnum
            keys = map.keys()
        elif isinstance(map, list):
            base = _ListEnum
            keys = map
        else:
            raise TypeError, "Enum map must be list or dict (got %s)" % map
        classname = "Enum%04d" % Enum.counter
        Enum.counter += 1
        # New class derives from selected base, and gets a 'map'
        # attribute containing the specified list or dict.
        return type.__new__(cls, classname, (base,), { 'map': map })


#
# "Constants"... handy aliases for various values.
#

# For compatibility with C++ bool constants.
false = False
true = True

# Some memory range specifications use this as a default upper bound.
MAX_ADDR = 2 ** 63

# For power-of-two sizing, e.g. 64*K gives an integer value 65536.
K = 1024
M = K*K
G = K*M

#####################################################################
#
# Object description loading.
#
# The final step is to define the classes corresponding to M5 objects
# and their parameters.  These classes are described in .odesc files
# in the source tree.  This code walks the tree to find those files
# and loads up the descriptions (by evaluating them in pieces as
# Python code).
#
#
# Because SimObject classes inherit from other SimObject classes, and
# can use arbitrary other SimObject classes as parameter types, we
# have to do this in three steps:
#
# 1. Walk the tree to find all the .odesc files.  Note that the base
# of the filename *must* match the class name.  This step builds a
# mapping from class names to file paths.
#
# 2. Start generating empty class definitions (via def_class()) using
# the OBJECT field of the .odesc files to determine inheritance.
# def_class() recurses on demand to define needed base classes before
# derived classes.
#
# 3. Now that all of the classes are defined, go through the .odesc
# files one more time loading the parameter descriptions.
#
#####################################################################

# dictionary: maps object names to file paths
odesc_file = {}

# dictionary: maps object names to boolean flag indicating whether
# class definition was loaded yet.  Since SimObject is defined in
# m5.config.py, count it as loaded.
odesc_loaded = { 'SimObject': True }

# Find odesc files in namelist and initialize odesc_file and
# odesc_loaded dictionaries.  Called via os.path.walk() (see below).
def find_odescs(process, dirpath, namelist):
    # Prune out SCCS directories so we don't process s.*.odesc files.
    i = 0
    while i < len(namelist):
        if namelist[i] == "SCCS":
            del namelist[i]
        else:
            i = i + 1
    # Find .odesc files and record them.
    for name in namelist:
        if name.endswith('.odesc'):
            objname = name[:name.rindex('.odesc')]
            path = os.path.join(dirpath, name)
            if odesc_file.has_key(objname):
                print "Warning: duplicate object names:", \
                      odesc_file[objname], path
            odesc_file[objname] = path
            odesc_loaded[objname] = False


# Regular expression string for parsing .odesc files.
file_re_string = r'''
^OBJECT: \s* (\w+) \s* \( \s* (\w+) \s* \)
\s*
^PARAMS: \s*\n ( (\s+.*\n)* )
'''

# Compiled regular expression object.
file_re = re.compile(file_re_string, re.MULTILINE | re.VERBOSE)

# .odesc file parsing function.  Takes a filename and returns tuple of
# object name, object base, and parameter description section.
def parse_file(path):
    f = open(path, 'r').read()
    m = file_re.search(f)
    if not m:
        print "Can't parse", path
        sys.exit(1)
    return (m.group(1), m.group(2), m.group(3))

# Define SimObject class based on description in specified filename.
# Class itself is empty except for _name attribute; parameter
# descriptions will be loaded later.  Will recurse to define base
# classes as needed before defining specified class.
def def_class(path):
    # load & parse file
    (obj, parent, params) = parse_file(path)
    # check to see if base class is defined yet; define it if not
    if not odesc_loaded.has_key(parent):
        print "No .odesc file found for", parent
        sys.exit(1)
    if not odesc_loaded[parent]:
        def_class(odesc_file[parent])
    # define the class.
    s = "class %s(%s): _name = '%s'" % (obj, parent, obj)
    try:
        # execute in global namespace, so new class will be globally
        # visible
        exec s in globals()
    except Exception, exc:
        print "Object error in %s:" % path, exc
    # mark this file as loaded
    odesc_loaded[obj] = True

# Munge an arbitrary Python code string to get it to execute (mostly
# dealing with indentation).  Stolen from isa_parser.py... see
# comments there for a more detailed description.
def fixPythonIndentation(s):
    # get rid of blank lines first
    s = re.sub(r'(?m)^\s*\n', '', s);
    if (s != '' and re.match(r'[ \t]', s[0])):
        s = 'if 1:\n' + s
    return s

# Load parameter descriptions from .odesc file.  Object class must
# already be defined.
def def_params(path):
    # load & parse file
    (obj_name, parent_name, param_code) = parse_file(path)
    # initialize param dict
    param_dict = {}
    # execute parameter descriptions.
    try:
        # "in globals(), param_dict" makes exec use the current
        # globals as the global namespace (so all of the Param
        # etc. objects are visible) and param_dict as the local
        # namespace (so the newly defined parameter variables will be
        # entered into param_dict).
        exec fixPythonIndentation(param_code) in globals(), param_dict
    except Exception, exc:
        print "Param error in %s:" % path, exc
        return
    # Convert object name string to Python class object
    obj = eval(obj_name)
    # Set the object's parameter description dictionary (see MetaConfigNode).
    obj.set_param_dict(param_dict)


# Walk directory tree to find .odesc files.
# Someday we'll have to make the root path an argument instead of
# hard-coding it.  For now the assumption is you're running this in
# util/config.
root = '../..'
os.path.walk(root, find_odescs, None)

# Iterate through file dictionary and define classes.
for objname, path in odesc_file.iteritems():
    if not odesc_loaded[objname]:
        def_class(path)

# Iterate through files again and load parameters.
for path in odesc_file.itervalues():
    def_params(path)

#####################################################################

# The final hook to generate .ini files.  Called from configuration
# script once config is built.
def instantiate(*objs):
    for obj in objs:
        obj._instantiate()
