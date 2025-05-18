import turtle
import random

class TreeNode:
    def __init__(self, length, is_leaf=False, angle=0):
        self.length = length
        self.is_leaf = is_leaf
        self.angle = angle
        self.children = []
    
    def add_child(self, node):
        self.children.append(node)

def generate_tree(depth, branch_length, leaf_length, branch_scale=0.95, leaf_scale=0.95):
    if depth == 0:
        return TreeNode(leaf_length, is_leaf=True)
    
    # Create root branch
    root = TreeNode(branch_length)
    
    # Randomly determine number of branches
    num_branches = random.randint(2, 5)
    
    # Generate child branches with random angles
    for _ in range(num_branches):
        # Random angle between -60 and 60 degrees
        branch_angle = random.randint(-60, 60)
        
        # Random scaling factor between 0.85 and 0.95
        random_scale = random.uniform(0.85, 0.95)
        
        # Some branches might be shorter
        length_modifier = random.uniform(0.8, 1.0)
        
        child = generate_tree(
            depth=depth-1,
            branch_length=branch_length*branch_scale*length_modifier,
            leaf_length=leaf_length*leaf_scale*length_modifier,
            branch_scale=branch_scale*random_scale,
            leaf_scale=leaf_scale*random_scale
        )
        
        child.angle = branch_angle
        root.add_child(child)
    
    return root

def draw_tree(node, t):
    # Save the current state
    position = t.position()
    heading = t.heading()
    
    # Set color based on node type
    if node.is_leaf:
        t.color('green')
        t.pensize(2)
    else:
        # Branches get thinner as they get shorter
        thickness = max(1, int(node.length / 10))
        t.pensize(thickness)
        t.color('brown')
    
    # Apply rotation
    t.left(node.angle)
    
    # Draw the current segment
    t.forward(node.length)
    
    # Draw all children
    for child in node.children:
        draw_tree(child, t)
    
    # Return to the original position and heading
    t.penup()
    t.goto(position)
    t.setheading(heading)
    t.pendown()

# Setup
t = turtle.Turtle()
t.speed(10)
screen = turtle.Screen()
t.left(90)  # Start upward

# Generate tree structure - deeper trees look more interesting
tree = generate_tree(depth=5, branch_length=50, leaf_length=30)

# Draw the tree
draw_tree(tree, t)

screen.exitonclick()