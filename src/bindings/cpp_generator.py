from clang.cindex import Index,CursorKind,TypeKind
from mako.template import Template
from sys import argv

CONSTRUCTER_SUFFIX = '_Create'
DESTRUCTER_SUFFIX = '_Destroy'

def get_type(type):
  if type.spelling == '_Bool':
    return 'bool'
  return type.spelling

class FunctionParameter:
  def __init__(self, cursor):
    type = cursor.type
    element_count = 0
    if type.kind == TypeKind.CONSTANTARRAY:
      type = type.element_type
      element_count = cursor.type.element_count
    self.type = get_type(type)
    self.element_count = element_count
    self.name = cursor.spelling

  @property
  def plain_type(self):
    return self.type.replace('*', '').strip()

  @property
  def without_namespace(self, namespace):
    return self.type[len(namespace):]

  @property
  def argument(self):
    return self.name

  @property
  def parameter(self):
    array = ''
    if self.element_count:
      array = '[%d]' % (self.element_count)
    return '%s %s%s' % (self.type, self.name, array)

class Function:
  def __init__(self, cursor):
    self.original_function = cursor.spelling
    self.result_type = get_type(cursor.result_type)
    self.brief_comment = cursor.brief_comment
    self.raw_comment = cursor.raw_comment

    token = cursor.spelling.split('_', 2)
    self.namespace = token[0][:4]
    self.type = token[0][4:]
    self.name = token[1]

    self.is_const = False
    self.with_exception = False

    if self.result_type == 'natsStatus':
      self.result_type = 'void'
      self.with_exception = True

    self.parameters = [FunctionParameter(p) for p in cursor.get_arguments()]

  @property
  def is_constructor(self):
    return self.original_function.endswith(CONSTRUCTER_SUFFIX)

  @property
  def is_destructor(self):
    return self.original_function.endswith(DESTRUCTER_SUFFIX)

class FunctionProxy:
  def __init__(self, function):
    self.function = function

  @property
  def namespace(self):
    return self.function.namespace

  @property
  def name(self):
    return self.function.name

  @property
  def original_function(self):
    return self.function.original_function

  @property
  def with_exception(self):
    return self.function.with_exception

  @property
  def brief_comment(self):
    return self.function.brief_comment

  @property
  def raw_comment(self):
    return self.function.raw_comment

  @property
  def invocation(self):
    ret = '%s(%s)' % (self.original_function, self.arguments)
    if self.function.with_exception:
      ret = 'Exception::CheckResult(%s)' % (ret)
    return ret

  @property
  def arguments(self):
    return ', '.join(self.arguments_)

  @property
  def parameters(self):
    return ', '.join(self.parameters_)

  @property
  def result_type(self):
    return self.function.result_type

  @property
  def suffix(self):
    if self.is_const:
      return ' const'
    return ''

class GlobalFunction(FunctionProxy):
  def __init__(self, function):
    FunctionProxy.__init__(self, function)
    self.arguments_ = [p.argument for p in function.parameters]
    self.parameters_ = [p.parameter for p in function.parameters]

class Method(FunctionProxy):
  def __init__(self, function, this, function_typedefs):
    FunctionProxy.__init__(self, function)
    self.arguments_ = []
    self.parameters_ = []
    self.forward_arguments_ = []
    self.forward_parameters_ = []

    self.is_constructor = function.is_constructor
    self.is_destructor = function.is_destructor
    self.temporary_object = False

    type_number = 0
    self.template_parameter_ = []
    next_parameter_type = None

    for p in function.parameters:
      argument = None
      parameter = None
      if p.type.replace('const ', '').startswith(this.original_type):
        if p.type.count('*') == 2:
          self.is_constructor = True
          argument = '&self'
        else:
          argument = 'self'
          self.is_const = p.type.startswith('const ')
      else:
        if p.type.count('*') == 2 and p.type.startswith(this.namespace):
          self.temporary_object = p.plain_type.replace(this.namespace, '')
          argument = '&ret.self'
        else:
          argument = p.argument
          parameter = p.parameter

      self.arguments_.append(argument)
      if parameter:
        self.parameters_.append(parameter)

        if p.type in function_typedefs:
          type_number = type_number + 1
          next_parameter_type = 'T%d' % (type_number)

          templateNamespace = p.type[:4]
          template = p.type[4:]

          if templateNamespace != self.namespace:
            template = '%s::%s' % (templateNamespace, template)

          args = (template, next_parameter_type, type_number)
          self.forward_arguments_.append('&%sCallback<%s, callback%d>' % args)
          self.template_parameter_ += [
            'typename %s' % (next_parameter_type),
            '%s<%s> callback%d' % args
          ]
        else:
          if next_parameter_type:
            self.forward_parameters_.append(parameter.replace('void', next_parameter_type))
            next_parameter_type = None
          else:
            self.forward_parameters_.append(parameter)
          self.forward_arguments_.append(argument)

  @property
  def result_type(self):
    if self.temporary_object:
      return self.temporary_object
    return self.function.result_type

  @property
  def forward_arguments(self):
    return ', '.join(self.forward_arguments_)

  @property
  def forward_parameters(self):
    return ', '.join(self.forward_parameters_)

  @property
  def template_parameter(self):
    return ', '.join(self.template_parameter_)


class Class:
  def __init__(self, namespace, typedef):
    self.original_type = typedef.name
    self.namespace = namespace.name
    self.brief_comment = typedef.brief_comment
    self.raw_comment = typedef.raw_comment
    self.name = typedef.name[4:]
    self.methods = []

  def add_method(self, function, function_typedefs):
    self.methods.append(Method(function, self, function_typedefs))

class Enum:
  def __init__(self, cursor):
    self.name = cursor.spelling

class FunctionTypedef:
  def __init__(self, cursor):
    self.original_name = cursor.spelling
    self.brief_comment = cursor.brief_comment
    self.raw_comment = cursor.raw_comment
    self.namespace = cursor.spelling[:4]
    self.name = cursor.spelling[4:]
    self.result_type = 'void'
    self.original_parameters = []
    self.wrapper = []
    self.arguments_ = []
    self.parameters_ = []

    for c in cursor.get_children():
      if c.kind == CursorKind.TYPE_REF:
        self.result_type = c.type.spelling
      else:
        self.original_parameters.append(FunctionParameter(c))

    self.has_closure = self.original_parameters[-1].name == 'closure'
    if not self.has_closure:
      return

    for p in self.original_parameters[:-1]:
      argument = p.argument
      parameter = p.parameter
      if p.type.startswith(self.namespace) and p.type != 'natsStatus':
        plain_type = p.plain_type
        if plain_type == 'natsMsg':
          argument = '%s(%s)' % (plain_type[4:], p.name)
          parameter = plain_type[4:] + ' &&'
        else:
          argument = p.name + '_'
          parameter = plain_type[4:] + ' &'
          args = (plain_type[4:], argument, p.name)
          self.wrapper.append('%s::WithoutDestuction %s(%s)' % args)

      self.arguments_.append(argument)
      self.parameters_.append(parameter)

  @property
  def arguments(self):
    return ', '.join(self.arguments_)

  @property
  def parameters(self):
    return ', '.join(self.parameters_)

  @property
  def callbackParameters(self):
    return ', '.join([p.parameter for p in self.original_parameters])

class Namespace:
  def __init__(self, name, include_guard = None):
    self.name = name
    self.classes = []
    self.functions = []
    self.function_typedefs = []
    self.include_guard = include_guard

  def add_class(self, name):
    c = Class(self, name)
    self.classes.append(c)
    return c

  def add_function(self, value):
    self.functions.append(GlobalFunction(value))

  def add_function_typedef(self, value):
    self.function_typedefs.append(value)

  def add_typedef(self, value):
    pass

class Typedef:
  def __init__(self, cursor):
    self.name = cursor.spelling
    self.original_name = cursor.spelling
    self.brief_comment = cursor.brief_comment
    self.raw_comment = cursor.raw_comment


def parse(cursor):
  functions = []
  enums = []
  typedefs = []
  function_typedefs = []

  for cursor in cursor.get_children():
    if not cursor.spelling[:4] in ['nats', 'stan']:
      continue
    if cursor.kind == CursorKind.FUNCTION_DECL:
      functions.append(Function(cursor))
    elif cursor.kind == CursorKind.TYPEDEF_DECL:
      children = [c for c in cursor.get_children()]
      if len(children) == 0:
        typedefs.append(Typedef(cursor))
        continue
      elif len(children) == 1:
        if children[0].kind == CursorKind.ENUM_DECL:
          enums.append(Enum(cursor))
          continue
        if children[0].kind == CursorKind.TYPE_REF:
          typedefs.append(Typedef(cursor))
          continue
      function_typedefs.append(FunctionTypedef(cursor))


  namespaces = [
    Namespace('nats'),
    Namespace('stan', 'defined(NATS_HAS_STREAMING)')
  ]
  function_names = [f.original_function for f in functions]
  function_typedef_names = [ftd.original_name for ftd in filter(lambda ftd: ftd.has_closure, function_typedefs)]
  classes = []

  for ns in namespaces:
    for ftd in filter(lambda ftd: ftd.original_name.startswith(ns.name), function_typedefs):
      if ftd.has_closure:
        ns.add_function_typedef(ftd)

    for td in filter(lambda td: td.name.startswith(ns.name), typedefs):
      if td.original_name + '_Destroy' in function_names:
        classes.append(ns.add_class(td))
      else:
        ns.add_typedef(td)

    for f in filter(lambda f: f.original_function.startswith(ns.name), functions):
      found_class = None
      for c in classes:
        if f.original_function.startswith(c.original_type):
          found_class = c
          break
      if found_class:
        found_class.add_method(f, function_typedef_names)
      else:
        ns.add_function(f)

  return namespaces

def main():
  if len(argv) != 4:
    print('usage: %s templatefile inputfile outputfile' % (argv[0]))
    exit(-1)

  index = Index.create()
  translation_unit = index.parse(None, [argv[2], '-DBUILD_IN_DOXYGEN', '-DNATS_HAS_STREAMING'])
  if not translation_unit:
    print('unable to load input')
    exit(-1)

  namespaces = parse(translation_unit.cursor)

  template = Template(filename=argv[1])
  f = open(argv[3], 'w')
  f.write(template.render(namespaces=namespaces))
  f.close()

if __name__ == '__main__':
    main()
